//! lolelffs-tools: Userspace tools for interacting with lolelffs filesystems
//!
//! This library provides functionality to read, write, and manipulate lolelffs
//! filesystem images without requiring the kernel module or mounting.

pub mod bitmap;
pub mod compress;
pub mod dir;
pub mod encrypt;
pub mod file;
pub mod fs;
pub mod types;
pub mod xattr;

pub use fs::LolelfFs;
pub use types::*;
