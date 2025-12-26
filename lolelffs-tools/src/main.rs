//! CLI tools for interacting with lolelffs filesystems

use anyhow::{bail, Context, Result};
use chrono::{TimeZone, Utc};
use clap::{Parser, Subcommand};
use lolelffs_tools::*;
use std::io::{self, Read, Write};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "lolelffs")]
#[command(about = "CLI tools for interacting with lolelffs filesystems")]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// List directory contents
    Ls {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path in the filesystem
        #[arg(default_value = "/")]
        path: String,

        /// Long listing format
        #[arg(short, long)]
        long: bool,

        /// Show all files including hidden
        #[arg(short, long)]
        all: bool,
    },

    /// Read file contents
    Cat {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file
        path: String,

        /// Password for encrypted filesystem
        #[arg(short = 'P', long)]
        password: Option<String>,
    },

    /// Write data to a file
    Write {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file
        path: String,

        /// Data to write (reads from stdin if not provided)
        #[arg(short, long)]
        data: Option<String>,

        /// Create file if it doesn't exist
        #[arg(short, long)]
        create: bool,

        /// Password for encrypted filesystem
        #[arg(short = 'P', long)]
        password: Option<String>,
    },

    /// Create a directory
    Mkdir {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to directory
        path: String,

        /// Create parent directories as needed
        #[arg(short, long)]
        parents: bool,
    },

    /// Remove a file or directory
    Rm {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,

        /// Remove directories recursively
        #[arg(short, long)]
        recursive: bool,

        /// Remove directory
        #[arg(short, long)]
        dir: bool,
    },

    /// Create an empty file
    Touch {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file
        path: String,
    },

    /// Show file or inode information
    Stat {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,
    },

    /// Create a new filesystem
    Mkfs {
        /// Filesystem image path
        image: PathBuf,

        /// Size in bytes (e.g., 1M, 10M, 100M)
        #[arg(short, long)]
        size: Option<String>,

        /// Enable encryption
        #[arg(short, long)]
        encrypt: bool,

        /// Password for encryption (will prompt if not provided and --encrypt is set)
        #[arg(short, long)]
        password: Option<String>,

        /// Encryption algorithm (aes-256-xts or chacha20-poly1305)
        #[arg(long, default_value = "aes-256-xts")]
        algo: String,

        /// PBKDF2 iterations
        #[arg(long, default_value = "100000")]
        iterations: u32,
    },

    /// Check filesystem integrity
    Fsck {
        /// Filesystem image path
        image: PathBuf,

        /// Verbose output
        #[arg(short, long)]
        verbose: bool,
    },

    /// Show filesystem statistics
    Df {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Human-readable sizes
        #[arg(short = 'H', long)]
        human: bool,
    },

    /// Create a link
    Ln {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Target path
        target: String,

        /// Link path
        link: String,

        /// Create symbolic link
        #[arg(short, long)]
        symbolic: bool,
    },

    /// Show superblock information
    Super {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,
    },

    /// Unlock encrypted filesystem
    Unlock {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Password for decryption
        #[arg(short, long)]
        password: Option<String>,
    },

    /// Copy file from host to filesystem
    Cp {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Source file on host
        source: PathBuf,

        /// Destination path in filesystem
        dest: String,

        /// Password for encrypted filesystem
        #[arg(short = 'P', long)]
        password: Option<String>,
    },

    /// Extract file from filesystem to host
    Extract {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Source path in filesystem
        source: String,

        /// Destination file on host
        dest: PathBuf,
    },

    /// Get an extended attribute value
    Getfattr {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,

        /// Attribute name (e.g., user.comment, security.selinux)
        name: String,

        /// Print value as hex dump
        #[arg(short = 'x', long)]
        hex: bool,
    },

    /// Set an extended attribute
    Setfattr {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,

        /// Attribute name (e.g., user.comment, security.selinux)
        #[arg(short, long)]
        name: String,

        /// Attribute value
        #[arg(short, long)]
        value: String,
    },

    /// List all extended attributes
    Listxattr {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,
    },

    /// Remove an extended attribute
    Removexattr {
        /// Filesystem image path
        #[arg(short, long)]
        image: PathBuf,

        /// Path to file or directory
        path: String,

        /// Attribute name
        name: String,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Ls {
            image,
            path,
            long,
            all,
        } => cmd_ls(&image, &path, long, all),
        Commands::Cat {
            image,
            path,
            password,
        } => cmd_cat(&image, &path, password),
        Commands::Write {
            image,
            path,
            data,
            create,
            password,
        } => cmd_write(&image, &path, data, create, password),
        Commands::Mkdir {
            image,
            path,
            parents,
        } => cmd_mkdir(&image, &path, parents),
        Commands::Rm {
            image,
            path,
            recursive,
            dir,
        } => cmd_rm(&image, &path, recursive, dir),
        Commands::Touch { image, path } => cmd_touch(&image, &path),
        Commands::Stat { image, path } => cmd_stat(&image, &path),
        Commands::Mkfs {
            image,
            size,
            encrypt,
            password,
            algo,
            iterations,
        } => cmd_mkfs(&image, size, encrypt, password, &algo, iterations),
        Commands::Fsck { image, verbose } => cmd_fsck(&image, verbose),
        Commands::Df { image, human } => cmd_df(&image, human),
        Commands::Ln {
            image,
            target,
            link,
            symbolic,
        } => cmd_ln(&image, &target, &link, symbolic),
        Commands::Super { image } => cmd_super(&image),
        Commands::Unlock { image, password } => cmd_unlock(&image, password),
        Commands::Cp {
            image,
            source,
            dest,
            password,
        } => cmd_cp(&image, &source, &dest, password),
        Commands::Extract {
            image,
            source,
            dest,
        } => cmd_extract(&image, &source, &dest),

        Commands::Getfattr {
            image,
            path,
            name,
            hex,
        } => cmd_getfattr(&image, &path, &name, hex),

        Commands::Setfattr {
            image,
            path,
            name,
            value,
        } => cmd_setfattr(&image, &path, &name, &value),

        Commands::Listxattr { image, path } => cmd_listxattr(&image, &path),

        Commands::Removexattr { image, path, name } => cmd_removexattr(&image, &path, &name),
    }
}

fn cmd_ls(image: &PathBuf, path: &str, long: bool, all: bool) -> Result<()> {
    let mut fs = LolelfFs::open_readonly(image)?;
    let inode_num = fs.resolve_path(path)?;

    let inode = fs.read_inode(inode_num)?;

    if inode.is_file() {
        // Just show the file itself
        let filename = path.rsplit('/').next().unwrap_or(path);
        if long {
            print_long_entry(filename, inode_num, &inode);
        } else {
            println!("{}", filename);
        }
        return Ok(());
    }

    let entries = fs.list_dir(inode_num)?;

    for entry in &entries {
        if !all && entry.filename.starts_with('.') {
            continue;
        }

        if long {
            print_long_entry(&entry.filename, entry.inode_num, &entry.inode);
        } else {
            println!("{}", entry.filename);
        }
    }

    Ok(())
}

fn print_long_entry(filename: &str, _inode_num: u32, inode: &Inode) {
    let mtime = Utc
        .timestamp_opt(inode.i_mtime as i64, 0)
        .single()
        .map(|dt| dt.format("%b %d %H:%M").to_string())
        .unwrap_or_else(|| "???".to_string());

    println!(
        "{}{} {:3} {:5} {:5} {:8} {} {}",
        inode.type_char(),
        inode.perm_string(),
        inode.i_nlink,
        inode.i_uid,
        inode.i_gid,
        inode.i_size,
        mtime,
        filename
    );
}

fn cmd_cat(image: &PathBuf, path: &str, password: Option<String>) -> Result<()> {
    let mut fs = LolelfFs::open_readonly(image)?;

    // Unlock if encrypted and password provided
    unlock_if_needed(&mut fs, password)?;

    let inode_num = fs.resolve_path(path)?;

    let data = fs.read_file(inode_num)?;
    io::stdout().write_all(&data)?;

    Ok(())
}

fn cmd_write(
    image: &PathBuf,
    path: &str,
    data: Option<String>,
    create: bool,
    password: Option<String>,
) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;

    // Unlock if encrypted and password provided
    unlock_if_needed(&mut fs, password)?;

    // Get the data to write
    let content = match data {
        Some(d) => d.into_bytes(),
        None => {
            let mut buf = Vec::new();
            io::stdin().read_to_end(&mut buf)?;
            buf
        }
    };

    // Try to resolve the path
    match fs.resolve_path(path) {
        Ok(inode_num) => {
            fs.write_file(inode_num, &content)?;
        }
        Err(_) if create => {
            // Create the file
            let (parent_path, filename) = split_path(path);
            let parent_inode = fs.resolve_path(&parent_path)?;
            let inode_num = fs.create_file(parent_inode, filename)?;
            fs.write_file(inode_num, &content)?;
        }
        Err(e) => return Err(e),
    }

    Ok(())
}

fn cmd_mkdir(image: &PathBuf, path: &str, parents: bool) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;

    if parents {
        // Create parent directories as needed
        let mut current = String::new();
        for component in path.trim_matches('/').split('/') {
            if component.is_empty() {
                continue;
            }
            current.push('/');
            current.push_str(component);

            if fs.resolve_path(&current).is_err() {
                let (parent_path, dirname) = split_path(&current);
                let parent_inode = fs.resolve_path(&parent_path)?;
                fs.mkdir(parent_inode, dirname)?;
            }
        }
    } else {
        let (parent_path, dirname) = split_path(path);
        let parent_inode = fs.resolve_path(&parent_path)?;
        fs.mkdir(parent_inode, dirname)?;
    }

    Ok(())
}

fn cmd_rm(image: &PathBuf, path: &str, recursive: bool, dir: bool) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let (parent_path, name) = split_path(path);
    let parent_inode = fs.resolve_path(&parent_path)?;

    let inode_num = fs
        .lookup(parent_inode, name)?
        .ok_or_else(|| anyhow::anyhow!("'{}' not found", path))?;

    let inode = fs.read_inode(inode_num)?;

    if inode.is_dir() {
        if !dir && !recursive {
            bail!("'{}' is a directory, use -d or -r flag", path);
        }

        if recursive {
            // Remove contents recursively
            remove_recursive(&mut fs, inode_num)?;
        }

        fs.rmdir(parent_inode, name)?;
    } else {
        fs.unlink(parent_inode, name)?;
    }

    Ok(())
}

fn remove_recursive(fs: &mut LolelfFs, dir_inode: u32) -> Result<()> {
    let entries = fs.list_dir(dir_inode)?;

    for entry in entries {
        if entry.inode.is_dir() {
            remove_recursive(fs, entry.inode_num)?;
            // The directory entry will be removed when we remove the parent
        }
        // Files will be removed when the directory is removed
    }

    Ok(())
}

fn cmd_touch(image: &PathBuf, path: &str) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;

    match fs.resolve_path(path) {
        Ok(inode_num) => {
            // Update timestamps
            let mut inode = fs.read_inode(inode_num)?;
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs() as u32;
            inode.i_atime = now;
            inode.i_mtime = now;
            fs.write_inode(inode_num, &inode)?;
        }
        Err(_) => {
            // Create the file
            let (parent_path, filename) = split_path(path);
            let parent_inode = fs.resolve_path(&parent_path)?;
            fs.create_file(parent_inode, filename)?;
        }
    }

    Ok(())
}

fn cmd_stat(image: &PathBuf, path: &str) -> Result<()> {
    let mut fs = LolelfFs::open_readonly(image)?;
    let inode_num = fs.resolve_path(path)?;
    let inode = fs.read_inode(inode_num)?;

    let file_type = if inode.is_dir() {
        "directory"
    } else if inode.is_symlink() {
        "symbolic link"
    } else {
        "regular file"
    };

    println!("  File: {}", path);
    println!(
        "  Size: {:<15} Blocks: {:<10} {}",
        inode.i_size, inode.i_blocks, file_type
    );
    println!("Inode: {:<15} Links: {}", inode_num, inode.i_nlink);
    println!(
        " Mode: {:o}/{}{:<9} Uid: {:5} Gid: {:5}",
        inode.i_mode,
        inode.type_char(),
        inode.perm_string(),
        inode.i_uid,
        inode.i_gid
    );

    let atime = format_timestamp(inode.i_atime);
    let mtime = format_timestamp(inode.i_mtime);
    let ctime = format_timestamp(inode.i_ctime);

    println!("Access: {}", atime);
    println!("Modify: {}", mtime);
    println!("Change: {}", ctime);

    if inode.is_symlink() {
        let target: String = inode
            .i_data
            .iter()
            .take_while(|&&b| b != 0)
            .map(|&b| b as char)
            .collect();
        println!("Target: {}", target);
    }

    if inode.ei_block != 0 {
        println!("Extent Block: {}", inode.ei_block);
    }

    Ok(())
}

fn cmd_mkfs(
    image: &PathBuf,
    size: Option<String>,
    encrypt: bool,
    password: Option<String>,
    algo: &str,
    iterations: u32,
) -> Result<()> {
    let size_bytes = match size {
        Some(s) => parse_size(&s)?,
        None => {
            // Check if file exists and use its size
            let meta = std::fs::metadata(image).with_context(|| {
                format!(
                    "Cannot stat '{}', specify --size to create",
                    image.display()
                )
            })?;
            meta.len()
        }
    };

    if size_bytes < LOLELFFS_MIN_BLOCKS as u64 * LOLELFFS_BLOCK_SIZE as u64 {
        bail!(
            "Filesystem too small: minimum {} bytes",
            LOLELFFS_MIN_BLOCKS as u64 * LOLELFFS_BLOCK_SIZE as u64
        );
    }

    // Handle encryption if requested
    let enc_config = if encrypt {
        // Get password
        let pwd = match password {
            Some(p) => p,
            None => {
                eprint!("Enter encryption password: ");
                io::stderr().flush()?;
                let mut pwd = String::new();
                io::stdin().read_line(&mut pwd)?;
                pwd.trim().to_string()
            }
        };

        if pwd.is_empty() {
            bail!("Password cannot be empty");
        }

        // Parse algorithm
        let enc_algo = match algo {
            "aes-256-xts" => LOLELFFS_ENC_AES256_XTS,
            "chacha20-poly1305" => LOLELFFS_ENC_CHACHA20_POLY,
            _ => bail!("Unknown encryption algorithm: {}", algo),
        };

        Some((pwd, enc_algo, iterations))
    } else {
        None
    };

    let fs = LolelfFs::create_with_encryption(image, size_bytes, enc_config)?;
    let stats = fs.statfs();

    println!("Created lolelffs filesystem on {}", image.display());
    println!("  Total size: {} bytes", stats.total_size());
    println!("  Block size: {} bytes", stats.block_size);
    println!("  Total blocks: {}", stats.total_blocks);
    println!("  Total inodes: {}", stats.total_inodes);
    println!("  Free blocks: {}", stats.free_blocks);
    println!("  Free inodes: {}", stats.free_inodes);
    if encrypt {
        println!("  Encryption: enabled ({} with PBKDF2)", algo);
    }

    Ok(())
}

fn cmd_fsck(image: &PathBuf, verbose: bool) -> Result<()> {
    let mut fs = LolelfFs::open_readonly(image)?;
    let mut errors = 0;
    let mut warnings = 0;

    if verbose {
        println!("Checking filesystem: {}", image.display());
    }

    // Check magic number
    if fs.superblock.magic != LOLELFFS_MAGIC {
        println!("ERROR: Invalid magic number");
        errors += 1;
    } else if verbose {
        println!("Magic number: OK");
    }

    // Check superblock consistency
    let expected_istore = fs.superblock.nr_inodes / LOLELFFS_INODES_PER_BLOCK;
    if fs.superblock.nr_istore_blocks != expected_istore {
        println!(
            "WARNING: Inode store blocks mismatch: {} vs expected {}",
            fs.superblock.nr_istore_blocks, expected_istore
        );
        warnings += 1;
    }

    // Check root inode
    let root_inode = fs.read_inode(LOLELFFS_ROOT_INO)?;
    if !root_inode.is_dir() {
        println!("ERROR: Root inode is not a directory");
        errors += 1;
    } else if verbose {
        println!("Root inode: OK");
    }

    if root_inode.ei_block == 0 {
        println!("ERROR: Root inode has no extent index block");
        errors += 1;
    } else if verbose {
        println!("Root extent index: OK");
    }

    // Check free counts are reasonable
    if fs.superblock.nr_free_inodes > fs.superblock.nr_inodes {
        println!("ERROR: Free inodes > total inodes");
        errors += 1;
    }

    if fs.superblock.nr_free_blocks > fs.superblock.nr_blocks {
        println!("ERROR: Free blocks > total blocks");
        errors += 1;
    }

    // Verify we can traverse the root directory
    match fs.list_dir(LOLELFFS_ROOT_INO) {
        Ok(entries) => {
            if verbose {
                println!("Root directory: {} entries", entries.len());
            }

            // Check each entry
            for entry in &entries {
                match fs.read_inode(entry.inode_num) {
                    Ok(_) => {
                        if verbose {
                            println!("  {}: inode {} OK", entry.filename, entry.inode_num);
                        }
                    }
                    Err(e) => {
                        println!(
                            "ERROR: Cannot read inode {} for '{}': {}",
                            entry.inode_num, entry.filename, e
                        );
                        errors += 1;
                    }
                }
            }
        }
        Err(e) => {
            println!("ERROR: Cannot list root directory: {}", e);
            errors += 1;
        }
    }

    println!();
    if errors > 0 {
        println!(
            "Filesystem check FAILED: {} errors, {} warnings",
            errors, warnings
        );
        std::process::exit(1);
    } else if warnings > 0 {
        println!("Filesystem check completed with {} warnings", warnings);
    } else {
        println!("Filesystem check passed");
    }

    Ok(())
}

fn cmd_df(image: &PathBuf, human: bool) -> Result<()> {
    let fs = LolelfFs::open_readonly(image)?;
    let stats = fs.statfs();

    let used = stats.total_blocks - stats.free_blocks;
    let use_percent = if stats.total_blocks > 0 {
        (used as f64 / stats.total_blocks as f64 * 100.0) as u32
    } else {
        0
    };

    if human {
        println!("Filesystem      Size  Used Avail Use%");
        println!(
            "{:<15} {:>5} {:>5} {:>5} {:>3}%",
            image.display(),
            format_size(stats.total_size()),
            format_size(stats.used_size()),
            format_size(stats.free_size()),
            use_percent
        );
    } else {
        println!("Filesystem      Blocks   Used   Avail Use%");
        println!(
            "{:<15} {:>6} {:>6} {:>7} {:>3}%",
            image.display(),
            stats.total_blocks,
            used,
            stats.free_blocks,
            use_percent
        );
    }

    println!();
    println!(
        "Inodes: {} total, {} free",
        stats.total_inodes, stats.free_inodes
    );

    Ok(())
}

fn cmd_ln(image: &PathBuf, target: &str, link: &str, symbolic: bool) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let (parent_path, link_name) = split_path(link);
    let parent_inode = fs.resolve_path(&parent_path)?;

    if symbolic {
        fs.symlink(parent_inode, link_name, target)?;
    } else {
        let target_inode = fs.resolve_path(target)?;
        fs.link(target_inode, parent_inode, link_name)?;
    }

    Ok(())
}

fn cmd_super(image: &PathBuf) -> Result<()> {
    let fs = LolelfFs::open_readonly(image)?;
    let sb = &fs.superblock;

    println!("Superblock information for {}", image.display());
    println!("  Magic: 0x{:08X}", sb.magic);
    println!("  Total blocks: {}", sb.nr_blocks);
    println!("  Total inodes: {}", sb.nr_inodes);
    println!("  Inode store blocks: {}", sb.nr_istore_blocks);
    println!("  Inode free bitmap blocks: {}", sb.nr_ifree_blocks);
    println!("  Block free bitmap blocks: {}", sb.nr_bfree_blocks);
    println!("  Free inodes: {}", sb.nr_free_inodes);
    println!("  Free blocks: {}", sb.nr_free_blocks);
    println!();
    println!("Extent limits:");
    println!(
        "  Max blocks per extent (with metadata): {}",
        sb.max_extent_blocks
    );
    println!(
        "  Max blocks per extent (large): {}",
        sb.max_extent_blocks_large
    );
    println!();
    println!("Features:");
    println!("  Compression features: 0x{:04X}", sb.comp_features);
    if sb.comp_features & LOLELFFS_FEATURE_LARGE_EXTENTS != 0 {
        println!("    - Large extents support enabled");
    }
    println!();
    println!("Layout:");
    println!("  Block 0: Superblock");
    println!(
        "  Blocks {}-{}: Inode store",
        sb.inode_store_start(),
        sb.ifree_bitmap_start() - 1
    );
    println!(
        "  Blocks {}-{}: Inode free bitmap",
        sb.ifree_bitmap_start(),
        sb.bfree_bitmap_start() - 1
    );
    println!(
        "  Blocks {}-{}: Block free bitmap",
        sb.bfree_bitmap_start(),
        sb.data_block_start() - 1
    );
    println!(
        "  Blocks {}-{}: Data blocks",
        sb.data_block_start(),
        sb.nr_blocks - 1
    );

    Ok(())
}

fn cmd_unlock(image: &PathBuf, password: Option<String>) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;

    // Check if encryption is enabled
    if fs.superblock.enc_enabled == 0 {
        println!("Filesystem is not encrypted");
        return Ok(());
    }

    // Check if already unlocked
    if fs.enc_unlocked {
        println!("Filesystem is already unlocked");
        return Ok(());
    }

    // Get password
    let pwd = match password {
        Some(p) => p,
        None => {
            eprint!("Enter password: ");
            io::stderr().flush()?;
            let mut pwd = String::new();
            io::stdin().read_line(&mut pwd)?;
            pwd.trim().to_string()
        }
    };

    // Unlock the filesystem
    fs.unlock(&pwd)?;

    println!("Filesystem unlocked successfully");
    println!(
        "  Encryption algorithm: {}",
        crate::encrypt::get_algo_name(fs.superblock.enc_default_algo as u8)
    );

    Ok(())
}

fn cmd_cp(image: &PathBuf, source: &PathBuf, dest: &str, password: Option<String>) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;

    // Unlock if encrypted and password provided
    unlock_if_needed(&mut fs, password)?;

    // Read source file from host
    let content =
        std::fs::read(source).with_context(|| format!("Failed to read '{}'", source.display()))?;

    // Determine destination path
    let dest_path = if dest.ends_with('/') {
        // Destination is a directory, use source filename
        let filename = source
            .file_name()
            .ok_or_else(|| anyhow::anyhow!("Invalid source filename"))?
            .to_string_lossy();
        format!("{}{}", dest, filename)
    } else {
        dest.to_string()
    };

    // Create or overwrite file
    match fs.resolve_path(&dest_path) {
        Ok(inode_num) => {
            fs.write_file(inode_num, &content)?;
        }
        Err(_) => {
            let (parent_path, filename) = split_path(&dest_path);
            let parent_inode = fs.resolve_path(&parent_path)?;
            let inode_num = fs.create_file(parent_inode, filename)?;
            fs.write_file(inode_num, &content)?;
        }
    }

    Ok(())
}

fn cmd_extract(image: &PathBuf, source: &str, dest: &PathBuf) -> Result<()> {
    let mut fs = LolelfFs::open_readonly(image)?;
    let inode_num = fs.resolve_path(source)?;
    let data = fs.read_file(inode_num)?;

    std::fs::write(dest, &data).with_context(|| format!("Failed to write '{}'", dest.display()))?;

    Ok(())
}

fn cmd_getfattr(image: &PathBuf, path: &str, name: &str, hex: bool) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let inode_num = fs.resolve_path(path)?;

    let value = fs.get_xattr(inode_num, name)?;

    println!("# file: {}", path);
    if hex || value.iter().any(|&b| b < 32 && b != b'\n' && b != b'\t') {
        // Print as hex if requested or if binary data
        print!("{}=0x", name);
        for byte in &value {
            print!("{:02x}", byte);
        }
        println!();
    } else {
        // Print as string
        match std::str::from_utf8(&value) {
            Ok(s) => println!("{}=\"{}\"", name, s),
            Err(_) => {
                print!("{}=0x", name);
                for byte in &value {
                    print!("{:02x}", byte);
                }
                println!();
            }
        }
    }

    Ok(())
}

fn cmd_setfattr(image: &PathBuf, path: &str, name: &str, value: &str) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let inode_num = fs.resolve_path(path)?;

    fs.set_xattr(inode_num, name, value.as_bytes())?;
    println!("Set {} on {}", name, path);

    Ok(())
}

fn cmd_listxattr(image: &PathBuf, path: &str) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let inode_num = fs.resolve_path(path)?;

    let xattrs = fs.list_xattrs(inode_num)?;

    if xattrs.is_empty() {
        println!("# file: {}", path);
        println!("(no extended attributes)");
    } else {
        println!("# file: {}", path);
        for xattr in xattrs {
            println!("{}", xattr);
        }
    }

    Ok(())
}

fn cmd_removexattr(image: &PathBuf, path: &str, name: &str) -> Result<()> {
    let mut fs = LolelfFs::open(image)?;
    let inode_num = fs.resolve_path(path)?;

    fs.remove_xattr(inode_num, name)?;
    println!("Removed {} from {}", name, path);

    Ok(())
}

// Helper functions

fn split_path(path: &str) -> (String, &str) {
    let path = path.trim_end_matches('/');
    match path.rfind('/') {
        Some(0) => ("/".to_string(), &path[1..]),
        Some(idx) => (path[..idx].to_string(), &path[idx + 1..]),
        None => ("/".to_string(), path),
    }
}

/// Unlock filesystem if it's encrypted and password is provided
fn unlock_if_needed(fs: &mut LolelfFs, password: Option<String>) -> Result<()> {
    // Check if filesystem is encrypted
    if fs.superblock.enc_enabled == 0 {
        return Ok(());
    }

    // If already unlocked, nothing to do
    if fs.enc_unlocked {
        return Ok(());
    }

    // Need password to unlock
    let pwd = match password {
        Some(p) => p,
        None => bail!("Filesystem is encrypted, please provide --password"),
    };

    fs.unlock(&pwd)?;
    Ok(())
}

fn parse_size(s: &str) -> Result<u64> {
    let s = s.trim();
    let (num_str, multiplier) = if s.ends_with('K') || s.ends_with('k') {
        (&s[..s.len() - 1], 1024u64)
    } else if s.ends_with('M') || s.ends_with('m') {
        (&s[..s.len() - 1], 1024 * 1024)
    } else if s.ends_with('G') || s.ends_with('g') {
        (&s[..s.len() - 1], 1024 * 1024 * 1024)
    } else {
        (s, 1)
    };

    let num: u64 = num_str
        .parse()
        .with_context(|| format!("Invalid size: {}", s))?;
    Ok(num * multiplier)
}

fn format_size(bytes: u64) -> String {
    if bytes >= 1024 * 1024 * 1024 {
        format!("{:.1}G", bytes as f64 / (1024.0 * 1024.0 * 1024.0))
    } else if bytes >= 1024 * 1024 {
        format!("{:.1}M", bytes as f64 / (1024.0 * 1024.0))
    } else if bytes >= 1024 {
        format!("{:.1}K", bytes as f64 / 1024.0)
    } else {
        format!("{}B", bytes)
    }
}

fn format_timestamp(ts: u32) -> String {
    Utc.timestamp_opt(ts as i64, 0)
        .single()
        .map(|dt| dt.format("%Y-%m-%d %H:%M:%S").to_string())
        .unwrap_or_else(|| "???".to_string())
}
