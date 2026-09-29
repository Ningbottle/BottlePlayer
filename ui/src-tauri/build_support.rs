use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};

/// Replace a runtime only after its complete contents have been staged.
/// A missing source or a locked destination must fail the build and preserve
/// the previous runtime. Identical files need no replacement.
pub fn copy_runtime_dll(source: &Path, destination: &Path) -> io::Result<()> {
    let contents = fs::read(source)?;
    if contents.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "empty native runtime",
        ));
    }
    if fs::read(destination).ok().as_deref() == Some(contents.as_slice()) {
        return Ok(());
    }

    static NEXT_STAGE: AtomicUsize = AtomicUsize::new(0);
    let file_name = destination.file_name().ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "runtime destination has no file name",
        )
    })?;
    let staged = destination.with_file_name(format!(
        ".{}.{}.{}.tmp",
        file_name.to_string_lossy(),
        std::process::id(),
        NEXT_STAGE.fetch_add(1, Ordering::Relaxed)
    ));
    let mut staged_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&staged)?;
    let cleanup = StagedFile(staged);
    let written = staged_file
        .write_all(&contents)
        .and_then(|_| staged_file.sync_all());
    drop(staged_file);
    written?;
    fs::rename(&cleanup.0, destination)?;
    Ok(())
}

struct StagedFile(PathBuf);

impl Drop for StagedFile {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Fixture(PathBuf);

    impl Fixture {
        fn new() -> Self {
            static NEXT_FIXTURE: AtomicUsize = AtomicUsize::new(0);
            let directory = std::env::temp_dir().join(format!(
                "bottlemusic-runtime-copy-{}-{}",
                std::process::id(),
                NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&directory).unwrap();
            Self(directory)
        }

        fn file(&self, name: &str, contents: &[u8]) -> PathBuf {
            let path = self.0.join(name);
            fs::write(&path, contents).unwrap();
            path
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            // Only remove files created in this fixture, never a recursive path.
            for entry in fs::read_dir(&self.0).unwrap() {
                let entry = entry.unwrap();
                if entry.file_type().unwrap().is_file() {
                    let _ = fs::remove_file(entry.path());
                }
            }
            let _ = fs::remove_dir(&self.0);
        }
    }

    #[test]
    fn missing_source_preserves_existing_runtime() {
        let fixture = Fixture::new();
        let destination = fixture.file("runtime.dll", b"previous runtime");
        assert!(copy_runtime_dll(&fixture.0.join("missing.dll"), &destination).is_err());
        assert_eq!(fs::read(destination).unwrap(), b"previous runtime");
    }

    #[test]
    fn empty_source_is_not_a_runtime() {
        let fixture = Fixture::new();
        let source = fixture.file("empty.dll", b"");
        let destination = fixture.file("runtime.dll", b"previous runtime");
        assert!(copy_runtime_dll(&source, &destination).is_err());
        assert_eq!(fs::read(destination).unwrap(), b"previous runtime");
    }

    #[test]
    fn replacement_contains_all_source_bytes_and_leaves_no_staging_files() {
        let fixture = Fixture::new();
        let source = fixture.file("new.dll", b"complete replacement runtime");
        let destination = fixture.file("runtime.dll", b"previous runtime");
        copy_runtime_dll(&source, &destination).unwrap();
        assert_eq!(
            fs::read(destination).unwrap(),
            b"complete replacement runtime"
        );
        assert_eq!(fs::read_dir(&fixture.0).unwrap().count(), 2);
    }

    #[test]
    fn copying_runtime_to_itself_preserves_it() {
        let fixture = Fixture::new();
        let source = fixture.file("runtime.dll", b"existing runtime");
        copy_runtime_dll(&source, &source).unwrap();
        assert_eq!(fs::read(source).unwrap(), b"existing runtime");
    }

    #[cfg(windows)]
    #[test]
    fn locked_destination_fails_without_renaming_or_truncating_it() {
        use std::os::windows::fs::OpenOptionsExt;
        let fixture = Fixture::new();
        let source = fixture.file("new.dll", b"new runtime");
        let destination = fixture.file("runtime.dll", b"previous runtime");
        let lock = OpenOptions::new()
            .read(true)
            .share_mode(1)
            .open(&destination)
            .unwrap();
        assert!(copy_runtime_dll(&source, &destination).is_err());
        assert_eq!(fs::read(&destination).unwrap(), b"previous runtime");
        assert_eq!(fs::read_dir(&fixture.0).unwrap().count(), 2);
        drop(lock);
    }
}
