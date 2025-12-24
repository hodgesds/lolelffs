use anyhow::{Context, Result};
use clap::Parser;
use fuser::{
    FileAttr, FileType, Filesystem, MountOption, ReplyAttr, ReplyData, ReplyDirectory, ReplyEntry,
    ReplyStatfs, ReplyWrite, Request, TimeOrNow,
};
use libc::{c_int, EEXIST, EISDIR, ENOENT, ENOSPC, ENOTDIR, ENOTEMPTY, ENOTSUP};
use log::{debug, error, info, warn};
use lolelffs_tools::{Inode, LolelfFs, LOLELFFS_BLOCK_SIZE, LOLELFFS_ROOT_INO};
use std::ffi::OsStr;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

// FUSE uses inode 1 as root, but lolelffs uses inode 0
// We need to translate between the two
const FUSE_ROOT_INO: u64 = 1;

fn fuse_to_lolelffs_ino(fuse_ino: u64) -> u32 {
    if fuse_ino == FUSE_ROOT_INO {
        LOLELFFS_ROOT_INO
    } else {
        (fuse_ino - 1) as u32
    }
}

fn lolelffs_to_fuse_ino(lolelffs_ino: u32) -> u64 {
    if lolelffs_ino == LOLELFFS_ROOT_INO {
        FUSE_ROOT_INO
    } else {
        (lolelffs_ino + 1) as u64
    }
}

/// FUSE driver for lolelffs filesystems
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Filesystem image path (can be raw image or ELF binary)
    image: PathBuf,

    /// Mount point directory
    mountpoint: PathBuf,

    /// Mount read-only
    #[arg(short, long)]
    ro: bool,

    /// Run in foreground (don't daemonize)
    #[arg(short, long)]
    foreground: bool,

    /// Enable debug logging
    #[arg(short, long)]
    debug: bool,
}

/// Main FUSE filesystem structure
struct LolelfFuseFs {
    fs: Arc<Mutex<LolelfFs>>,
    read_only: bool,
}

impl LolelfFuseFs {
    fn new(fs: LolelfFs, read_only: bool) -> Self {
        LolelfFuseFs {
            fs: Arc::new(Mutex::new(fs)),
            read_only,
        }
    }
}

/// Convert lolelffs Inode to FUSE FileAttr
fn inode_to_attr(ino: u64, inode: &Inode) -> FileAttr {
    let kind = if inode.is_dir() {
        FileType::Directory
    } else if inode.is_symlink() {
        FileType::Symlink
    } else {
        FileType::RegularFile
    };

    FileAttr {
        ino,
        size: inode.i_size as u64,
        blocks: inode.i_blocks as u64,
        atime: UNIX_EPOCH + Duration::from_secs(inode.i_atime as u64),
        mtime: UNIX_EPOCH + Duration::from_secs(inode.i_mtime as u64),
        ctime: UNIX_EPOCH + Duration::from_secs(inode.i_ctime as u64),
        crtime: UNIX_EPOCH + Duration::from_secs(inode.i_ctime as u64), // Use ctime for creation
        kind,
        perm: (inode.i_mode & 0o7777) as u16,
        nlink: inode.i_nlink,
        uid: inode.i_uid,
        gid: inode.i_gid,
        rdev: 0,
        blksize: LOLELFFS_BLOCK_SIZE,
        flags: 0,
    }
}

/// Map anyhow errors to libc errno codes
fn map_error(e: &anyhow::Error) -> c_int {
    let msg = format!("{:#}", e);

    // Check for I/O errors first
    if let Some(io_err) = e.downcast_ref::<std::io::Error>() {
        return io_err.raw_os_error().unwrap_or(libc::EIO);
    }

    // Pattern match on error messages
    if msg.contains("not found") || msg.contains("No such") {
        ENOENT
    } else if msg.contains("not a directory") {
        ENOTDIR
    } else if msg.contains("is a directory") {
        EISDIR
    } else if msg.contains("already exists") {
        EEXIST
    } else if msg.contains("not empty") {
        ENOTEMPTY
    } else if msg.contains("No free") || msg.contains("full") {
        ENOSPC
    } else if msg.contains("permission") || msg.contains("denied") {
        libc::EACCES
    } else {
        libc::EIO
    }
}

/// Update inode timestamps
fn update_times(inode: &mut Inode, atime: bool, mtime: bool, ctime: bool) {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs() as u32;

    if atime {
        inode.i_atime = now;
    }
    if mtime {
        inode.i_mtime = now;
    }
    if ctime {
        inode.i_ctime = now;
    }
}

impl Filesystem for LolelfFuseFs {
    fn init(
        &mut self,
        _req: &Request<'_>,
        _config: &mut fuser::KernelConfig,
    ) -> Result<(), c_int> {
        info!("Initializing lolelffs FUSE filesystem");
        Ok(())
    }

    fn lookup(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: ReplyEntry) {
        debug!("lookup(parent={}, name={:?})", parent, name);

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(ENOENT);
                return;
            }
        };

        let parent_ino = fuse_to_lolelffs_ino(parent);
        let mut fs = self.fs.lock().unwrap();
        match fs.lookup(parent_ino, name_str) {
            Ok(Some(inode_num)) => {
                match fs.read_inode(inode_num) {
                    Ok(mut inode) => {
                        // Update atime
                        update_times(&mut inode, true, false, false);
                        if let Err(e) = fs.write_inode(inode_num, &inode) {
                            warn!("Failed to update atime: {}", e);
                        }

                        let fuse_ino = lolelffs_to_fuse_ino(inode_num);
                        let attr = inode_to_attr(fuse_ino, &inode);
                        let ttl = Duration::from_secs(1);
                        reply.entry(&ttl, &attr, 0);
                    }
                    Err(e) => {
                        error!("Failed to read inode {}: {}", inode_num, e);
                        reply.error(map_error(&e));
                    }
                }
            }
            Ok(None) => {
                debug!("lookup: file not found");
                reply.error(ENOENT);
            }
            Err(e) => {
                debug!("lookup failed: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn getattr(&mut self, _req: &Request, ino: u64, reply: ReplyAttr) {
        debug!("getattr(ino={})", ino);

        let lolelffs_ino = fuse_to_lolelffs_ino(ino);
        let mut fs = self.fs.lock().unwrap();
        match fs.read_inode(lolelffs_ino) {
            Ok(inode) => {
                let attr = inode_to_attr(ino, &inode);
                let ttl = Duration::from_secs(1);
                reply.attr(&ttl, &attr);
            }
            Err(e) => {
                error!("Failed to get attr for inode {}: {}", ino, e);
                reply.error(map_error(&e));
            }
        }
    }

    fn readdir(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        mut reply: ReplyDirectory,
    ) {
        debug!("readdir(ino={}, offset={})", ino, offset);

        let mut fs = self.fs.lock().unwrap();
        match fs.list_dir(fuse_to_lolelffs_ino(ino)) {
            Ok(entries) => {
                let mut idx = offset;

                // Add . and .. entries
                if offset == 0 {
                    if reply.add(ino, 0, FileType::Directory, ".") {
                        reply.ok();
                        return;
                    }
                    idx += 1;
                }

                if offset <= 1 {
                    let parent_ino = if ino == FUSE_ROOT_INO {
                        FUSE_ROOT_INO
                    } else {
                        // For now, use the current inode (TODO: track parent)
                        ino
                    };

                    if reply.add(parent_ino, 1, FileType::Directory, "..") {
                        reply.ok();
                        return;
                    }
                    idx += 1;
                }

                // Add actual entries
                for entry in entries.iter().skip((offset - 2).max(0) as usize) {
                    let file_ino = lolelffs_to_fuse_ino(entry.inode_num);
                    let kind = if entry.inode.is_dir() {
                        FileType::Directory
                    } else if entry.inode.is_symlink() {
                        FileType::Symlink
                    } else {
                        FileType::RegularFile
                    };

                    if reply.add(file_ino, idx + 1, kind, &entry.filename) {
                        break;
                    }
                    idx += 1;
                }

                reply.ok();
            }
            Err(e) => {
                error!("Failed to read directory {}: {}", ino, e);
                reply.error(map_error(&e));
            }
        }
    }

    fn read(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        size: u32,
        _flags: i32,
        _lock: Option<u64>,
        reply: ReplyData,
    ) {
        debug!("read(ino={}, offset={}, size={})", ino, offset, size);

        let mut fs = self.fs.lock().unwrap();
        match fs.read_file(fuse_to_lolelffs_ino(ino)) {
            Ok(data) => {
                let offset = offset as usize;
                let end = (offset + size as usize).min(data.len());

                if offset >= data.len() {
                    reply.data(&[]);
                } else {
                    reply.data(&data[offset..end]);
                }

                // Update atime
                if let Ok(mut inode) = fs.read_inode(fuse_to_lolelffs_ino(ino)) {
                    update_times(&mut inode, true, false, false);
                    if let Err(e) = fs.write_inode(fuse_to_lolelffs_ino(ino), &inode) {
                        warn!("Failed to update atime: {}", e);
                    }
                }
            }
            Err(e) => {
                error!("Failed to read file {}: {}", ino, e);
                reply.error(map_error(&e));
            }
        }
    }

    fn readlink(&mut self, _req: &Request, ino: u64, reply: ReplyData) {
        debug!("readlink(ino={})", ino);

        let mut fs = self.fs.lock().unwrap();
        match fs.read_inode(fuse_to_lolelffs_ino(ino)) {
            Ok(inode) => {
                if !inode.is_symlink() {
                    reply.error(libc::EINVAL);
                    return;
                }

                // Symlink target is stored in i_data
                let target = inode.i_data.iter()
                    .take_while(|&&b| b != 0)
                    .cloned()
                    .collect::<Vec<u8>>();

                reply.data(&target);
            }
            Err(e) => {
                error!("Failed to read symlink {}: {}", ino, e);
                reply.error(map_error(&e));
            }
        }
    }

    fn mknod(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        mode: u32,
        _umask: u32,
        _rdev: u32,
        reply: ReplyEntry,
    ) {
        debug!("mknod(parent={}, name={:?}, mode={:o})", parent, name, mode);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        // Only support regular files
        if (mode & libc::S_IFMT) != libc::S_IFREG {
            reply.error(ENOTSUP);
            return;
        }

        let mut fs = self.fs.lock().unwrap();
        match fs.create_file(fuse_to_lolelffs_ino(parent), name_str) {
            Ok(inode_num) => {
                match fs.read_inode(inode_num) {
                    Ok(mut inode) => {
                        // Set the mode
                        inode.i_mode = mode;
                        if let Err(e) = fs.write_inode(inode_num, &inode) {
                            warn!("Failed to set mode: {}", e);
                        }

                        let attr = inode_to_attr(lolelffs_to_fuse_ino(inode_num), &inode);
                        let ttl = Duration::from_secs(1);
                        reply.entry(&ttl, &attr, 0);
                    }
                    Err(e) => {
                        error!("Failed to read newly created inode: {}", e);
                        reply.error(map_error(&e));
                    }
                }
            }
            Err(e) => {
                error!("Failed to create file: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn mkdir(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        mode: u32,
        _umask: u32,
        reply: ReplyEntry,
    ) {
        debug!("mkdir(parent={}, name={:?}, mode={:o})", parent, name, mode);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        match fs.mkdir(fuse_to_lolelffs_ino(parent), name_str) {
            Ok(inode_num) => {
                match fs.read_inode(inode_num) {
                    Ok(mut inode) => {
                        // Update mode to include directory bit and permissions
                        inode.i_mode = libc::S_IFDIR | (mode & 0o7777);
                        if let Err(e) = fs.write_inode(inode_num, &inode) {
                            warn!("Failed to set mode: {}", e);
                        }

                        let attr = inode_to_attr(lolelffs_to_fuse_ino(inode_num), &inode);
                        let ttl = Duration::from_secs(1);
                        reply.entry(&ttl, &attr, 0);
                    }
                    Err(e) => {
                        error!("Failed to read newly created directory: {}", e);
                        reply.error(map_error(&e));
                    }
                }
            }
            Err(e) => {
                error!("Failed to create directory: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn unlink(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: fuser::ReplyEmpty) {
        debug!("unlink(parent={}, name={:?})", parent, name);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        match fs.unlink(fuse_to_lolelffs_ino(parent), name_str) {
            Ok(()) => reply.ok(),
            Err(e) => {
                error!("Failed to unlink file: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn rmdir(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: fuser::ReplyEmpty) {
        debug!("rmdir(parent={}, name={:?})", parent, name);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        match fs.rmdir(fuse_to_lolelffs_ino(parent), name_str) {
            Ok(()) => reply.ok(),
            Err(e) => {
                error!("Failed to remove directory: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn symlink(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        link: &std::path::Path,
        reply: ReplyEntry,
    ) {
        debug!("symlink(parent={}, name={:?}, link={:?})", parent, name, link);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let link_str = match link.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        match fs.symlink(fuse_to_lolelffs_ino(parent), name_str, link_str) {
            Ok(inode_num) => {
                match fs.read_inode(inode_num) {
                    Ok(inode) => {
                        let attr = inode_to_attr(lolelffs_to_fuse_ino(inode_num), &inode);
                        let ttl = Duration::from_secs(1);
                        reply.entry(&ttl, &attr, 0);
                    }
                    Err(e) => {
                        error!("Failed to read newly created symlink: {}", e);
                        reply.error(map_error(&e));
                    }
                }
            }
            Err(e) => {
                error!("Failed to create symlink: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn link(
        &mut self,
        _req: &Request,
        ino: u64,
        newparent: u64,
        newname: &OsStr,
        reply: ReplyEntry,
    ) {
        debug!("link(ino={}, newparent={}, newname={:?})", ino, newparent, newname);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match newname.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        match fs.link(fuse_to_lolelffs_ino(ino), fuse_to_lolelffs_ino(newparent), name_str) {
            Ok(()) => {
                match fs.read_inode(fuse_to_lolelffs_ino(ino)) {
                    Ok(inode) => {
                        let attr = inode_to_attr(ino, &inode);
                        let ttl = Duration::from_secs(1);
                        reply.entry(&ttl, &attr, 0);
                    }
                    Err(e) => {
                        error!("Failed to read inode after link: {}", e);
                        reply.error(map_error(&e));
                    }
                }
            }
            Err(e) => {
                error!("Failed to create hard link: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn write(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        data: &[u8],
        _write_flags: u32,
        _flags: i32,
        _lock_owner: Option<u64>,
        reply: ReplyWrite,
    ) {
        debug!("write(ino={}, offset={}, size={})", ino, offset, data.len());

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let mut fs = self.fs.lock().unwrap();

        // Read current file contents
        let mut file_data = match fs.read_file(fuse_to_lolelffs_ino(ino)) {
            Ok(d) => d,
            Err(e) => {
                // If file doesn't exist or is empty, start with empty vec
                debug!("File read returned error (may be empty): {}", e);
                Vec::new()
            }
        };

        // Extend file if necessary
        let offset = offset as usize;
        let end_pos = offset + data.len();
        if end_pos > file_data.len() {
            file_data.resize(end_pos, 0);
        }

        // Write data at offset
        file_data[offset..end_pos].copy_from_slice(data);

        // Write back to filesystem
        match fs.write_file(fuse_to_lolelffs_ino(ino), &file_data) {
            Ok(()) => {
                // Update mtime and ctime
                if let Ok(mut inode) = fs.read_inode(fuse_to_lolelffs_ino(ino)) {
                    update_times(&mut inode, false, true, true);
                    if let Err(e) = fs.write_inode(fuse_to_lolelffs_ino(ino), &inode) {
                        warn!("Failed to update timestamps: {}", e);
                    }
                }

                reply.written(data.len() as u32);
            }
            Err(e) => {
                error!("Failed to write file: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn setattr(
        &mut self,
        _req: &Request,
        ino: u64,
        mode: Option<u32>,
        uid: Option<u32>,
        gid: Option<u32>,
        size: Option<u64>,
        atime: Option<TimeOrNow>,
        mtime: Option<TimeOrNow>,
        _ctime: Option<SystemTime>,
        _fh: Option<u64>,
        _crtime: Option<SystemTime>,
        _chgtime: Option<SystemTime>,
        _bkuptime: Option<SystemTime>,
        _flags: Option<u32>,
        reply: ReplyAttr,
    ) {
        debug!("setattr(ino={})", ino);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let mut fs = self.fs.lock().unwrap();
        match fs.read_inode(fuse_to_lolelffs_ino(ino)) {
            Ok(mut inode) => {
                let mut modified = false;

                if let Some(m) = mode {
                    inode.i_mode = (inode.i_mode & libc::S_IFMT) | (m & 0o7777);
                    modified = true;
                }

                if let Some(u) = uid {
                    inode.i_uid = u;
                    modified = true;
                }

                if let Some(g) = gid {
                    inode.i_gid = g;
                    modified = true;
                }

                if let Some(s) = size {
                    if let Err(e) = fs.truncate(fuse_to_lolelffs_ino(ino), s as u32) {
                        error!("Failed to truncate file: {}", e);
                        reply.error(map_error(&e));
                        return;
                    }
                    // Re-read inode after truncate
                    match fs.read_inode(fuse_to_lolelffs_ino(ino)) {
                        Ok(i) => inode = i,
                        Err(e) => {
                            error!("Failed to re-read inode after truncate: {}", e);
                            reply.error(map_error(&e));
                            return;
                        }
                    }
                    modified = true;
                }

                if let Some(time) = atime {
                    match time {
                        TimeOrNow::Now => {
                            let now = SystemTime::now()
                                .duration_since(UNIX_EPOCH)
                                .unwrap()
                                .as_secs() as u32;
                            inode.i_atime = now;
                        }
                        TimeOrNow::SpecificTime(t) => {
                            let timestamp = t.duration_since(UNIX_EPOCH).unwrap().as_secs() as u32;
                            inode.i_atime = timestamp;
                        }
                    }
                    modified = true;
                }

                if let Some(time) = mtime {
                    match time {
                        TimeOrNow::Now => {
                            let now = SystemTime::now()
                                .duration_since(UNIX_EPOCH)
                                .unwrap()
                                .as_secs() as u32;
                            inode.i_mtime = now;
                        }
                        TimeOrNow::SpecificTime(t) => {
                            let timestamp = t.duration_since(UNIX_EPOCH).unwrap().as_secs() as u32;
                            inode.i_mtime = timestamp;
                        }
                    }
                    modified = true;
                }

                if modified {
                    // Update ctime when metadata changes
                    update_times(&mut inode, false, false, true);

                    if let Err(e) = fs.write_inode(fuse_to_lolelffs_ino(ino), &inode) {
                        error!("Failed to write inode: {}", e);
                        reply.error(map_error(&e));
                        return;
                    }
                }

                let attr = inode_to_attr(ino, &inode);
                let ttl = Duration::from_secs(1);
                reply.attr(&ttl, &attr);
            }
            Err(e) => {
                error!("Failed to read inode for setattr: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn statfs(&mut self, _req: &Request, _ino: u64, reply: ReplyStatfs) {
        debug!("statfs()");

        let fs = self.fs.lock().unwrap();
        let stats = fs.statfs();

        reply.statfs(
            stats.total_blocks as u64,
            stats.free_blocks as u64,
            stats.free_blocks as u64,
            stats.total_inodes as u64,
            stats.free_inodes as u64,
            LOLELFFS_BLOCK_SIZE,
            255, // max filename length
            LOLELFFS_BLOCK_SIZE,
        );
    }

    fn getxattr(&mut self, _req: &Request, ino: u64, name: &OsStr, size: u32, reply: fuser::ReplyXattr) {
        debug!("getxattr(ino={}, name={:?}, size={})", ino, name, size);

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        let lolelffs_ino = fuse_to_lolelffs_ino(ino);

        match fs.get_xattr(lolelffs_ino, name_str) {
            Ok(value) => {
                if size == 0 {
                    // Caller wants to know the size
                    reply.size(value.len() as u32);
                } else if size < value.len() as u32 {
                    // Buffer too small
                    reply.error(libc::ERANGE);
                } else {
                    // Return the value
                    reply.data(&value);
                }
            }
            Err(e) => {
                debug!("getxattr error: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn setxattr(
        &mut self,
        _req: &Request,
        ino: u64,
        name: &OsStr,
        value: &[u8],
        _flags: i32,
        _position: u32,
        reply: fuser::ReplyEmpty,
    ) {
        debug!("setxattr(ino={}, name={:?}, value_len={})", ino, name, value.len());

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        let lolelffs_ino = fuse_to_lolelffs_ino(ino);

        match fs.set_xattr(lolelffs_ino, name_str, value) {
            Ok(()) => reply.ok(),
            Err(e) => {
                error!("setxattr error: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn listxattr(&mut self, _req: &Request, ino: u64, size: u32, reply: fuser::ReplyXattr) {
        debug!("listxattr(ino={}, size={})", ino, size);

        let mut fs = self.fs.lock().unwrap();
        let lolelffs_ino = fuse_to_lolelffs_ino(ino);

        match fs.list_xattrs(lolelffs_ino) {
            Ok(names) => {
                // Calculate total size needed (name + NUL for each)
                let total_size: usize = names.iter().map(|n| n.len() + 1).sum();

                if size == 0 {
                    // Caller wants to know the size
                    reply.size(total_size as u32);
                } else if size < total_size as u32 {
                    // Buffer too small
                    reply.error(libc::ERANGE);
                } else {
                    // Build the list of NUL-separated names
                    let mut data = Vec::with_capacity(total_size);
                    for name in names {
                        data.extend_from_slice(name.as_bytes());
                        data.push(0); // NUL terminator
                    }
                    reply.data(&data);
                }
            }
            Err(e) => {
                debug!("listxattr error: {}", e);
                reply.error(map_error(&e));
            }
        }
    }

    fn removexattr(&mut self, _req: &Request, ino: u64, name: &OsStr, reply: fuser::ReplyEmpty) {
        debug!("removexattr(ino={}, name={:?})", ino, name);

        if self.read_only {
            reply.error(libc::EROFS);
            return;
        }

        let name_str = match name.to_str() {
            Some(s) => s,
            None => {
                reply.error(libc::EINVAL);
                return;
            }
        };

        let mut fs = self.fs.lock().unwrap();
        let lolelffs_ino = fuse_to_lolelffs_ino(ino);

        match fs.remove_xattr(lolelffs_ino, name_str) {
            Ok(()) => reply.ok(),
            Err(e) => {
                error!("removexattr error: {}", e);
                reply.error(map_error(&e));
            }
        }
    }
}

fn main() -> Result<()> {
    let args = Args::parse();

    // Setup logging
    let log_level = if args.debug { "debug" } else { "info" };
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or(log_level)).init();

    info!("Opening lolelffs image: {:?}", args.image);

    // Try to open filesystem (read-write or read-only)
    let fs = if args.ro {
        info!("Mounting read-only");
        LolelfFs::open_readonly(&args.image)
            .with_context(|| format!("Failed to open filesystem image: {:?}", args.image))?
    } else {
        match LolelfFs::open(&args.image) {
            Ok(fs) => {
                info!("Mounting read-write");
                fs
            }
            Err(e) => {
                warn!("Failed to open read-write, trying read-only: {}", e);
                LolelfFs::open_readonly(&args.image)
                    .with_context(|| format!("Failed to open filesystem image: {:?}", args.image))?
            }
        }
    };

    let fuse_fs = LolelfFuseFs::new(fs, args.ro);

    let mut mount_options = vec![
        MountOption::FSName("lolelffs".to_string()),
    ];

    if args.ro {
        mount_options.push(MountOption::RO);
    }

    info!("Mounting at: {:?}", args.mountpoint);
    info!("Mount options: {:?}", mount_options);

    fuser::mount2(fuse_fs, &args.mountpoint, &mount_options)
        .with_context(|| format!("Failed to mount FUSE filesystem at {:?}", args.mountpoint))?;

    Ok(())
}
