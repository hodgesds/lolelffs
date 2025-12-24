//! lolelffs-tools: Userspace tools for interacting with lolelffs filesystems
//!
//! This library provides functionality to read, write, and manipulate lolelffs
//! filesystem images without requiring the kernel module or mounting.

pub mod types;
pub mod fs;
pub mod bitmap;
pub mod dir;
pub mod file;
pub mod xattr;
pub mod compress;
pub mod encrypt;

pub use types::*;
pub use fs::LolelfFs;
