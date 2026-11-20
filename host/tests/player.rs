//! The playerctl follower and probe against a fake playerctl script.

mod common;

use std::io;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use common::TempDir;
use nyxd::player::{probe, Follower, Snapshot, FORMAT};
use nyxd::protocol::Status;

/// Held while a script is written and while a child is forked: a fork made by another
/// test while a script's write fd is open keeps that fd until exec, and running the
/// script then fails with ETXTBSY ("Text file busy").
static FORK: Mutex<()> = Mutex::new(());

fn spawn(program: &Path) -> io::Result<Follower> {
    let _g = FORK.lock().unwrap_or_else(|e| e.into_inner());
    Follower::spawn(program)
}

fn probe_locked(program: &Path) -> io::Result<Snapshot> {
    let _g = FORK.lock().unwrap_or_else(|e| e.into_inner());
    probe(program)
}

/// A fake playerctl running `body`; its arguments go to `args` in the same directory.
fn fake(tmp: &TempDir, body: &str) -> PathBuf {
    let _g = FORK.lock().unwrap_or_else(|e| e.into_inner());
    let args = tmp.path().join("args");
    tmp.write("playerctl", format!("#!/bin/sh\nprintf '%s\\n' \"$@\" > {}\n{body}\n", args.display()));
    let script = tmp.path().join("playerctl");
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
    script
}

fn args(tmp: &TempDir) -> Vec<String> {
    std::fs::read_to_string(tmp.path().join("args")).unwrap().lines().map(String::from).collect()
}

fn line(status: &str, position_us: u64, title: &str) -> String {
    format!("spotify\x1fspotify\x1f{status}\x1f{position_us}\x1f200000000\x1f1.0\x1f{title}\x1fBand\x1fLP")
}

/// Everything the follower reports until playerctl exits.
fn follow_to_the_end(f: &mut Follower) -> Vec<Snapshot> {
    let mut seen = Vec::new();
    let start = Instant::now();
    loop {
        assert!(start.elapsed() < Duration::from_secs(5), "playerctl never exited");
        let mut pfd = libc::pollfd { fd: f.fd(), events: libc::POLLIN, revents: 0 };
        unsafe { libc::poll(&mut pfd, 1, 100) };
        match f.read() {
            Ok(Some(s)) => seen.push(s),
            Ok(None) => {}
            Err(e) => {
                assert_eq!(e.kind(), ErrorKind::UnexpectedEof);
                return seen;
            }
        }
    }
}

#[test]
fn follower_reports_lines_and_the_player_going_away() {
    let tmp = TempDir::new("player-follow");
    let body = format!(
        "printf '%s\\n' '{}'\nsleep 0.2\nprintf '%s\\n' 'junk' '{}'\nsleep 0.2\nprintf '%s' '{}'\nsleep 0.2\necho\nsleep 0.2",
        line("Playing", 1_000_000, "One"),
        line("Paused", 2_500_000, "Two"),
        line("Playing", 0, "half a line, then the rest"),
    );
    let mut f = spawn(&fake(&tmp, &body)).unwrap();
    let seen = follow_to_the_end(&mut f);
    assert_eq!(args(&tmp), ["--follow", "metadata", "--format", FORMAT]);
    let titles: Vec<&str> = seen.iter().map(|s| s.media.title.as_str()).collect();
    // "junk" is ignored; the cut line is completed by the newline of the empty line that follows it
    assert_eq!(titles, ["One", "Two", "half a line, then the rest"]);
    assert_eq!((seen[1].media.status, seen[1].media.position_s), (Status::Paused, 2));
    assert!(seen.iter().all(|s| s.media.present));
}

#[test]
fn empty_line_means_no_player() {
    let tmp = TempDir::new("player-gone");
    let body = format!("printf '%s\\n' '{}'\nsleep 0.2\necho\nsleep 0.2", line("Playing", 0, "x"));
    let mut f = spawn(&fake(&tmp, &body)).unwrap();
    let seen = follow_to_the_end(&mut f);
    assert_eq!(seen.last().unwrap(), &Snapshot::default());
}

#[test]
fn dropping_the_follower_stops_playerctl() {
    let tmp = TempDir::new("player-drop");
    let started = Instant::now();
    let f = spawn(&fake(&tmp, "exec sleep 30")).unwrap();
    drop(f); // kills and reaps: does not wait the 30 s
    assert!(started.elapsed() < Duration::from_secs(5));
}

#[test]
fn probe_reads_one_line_and_no_player_is_not_an_error() {
    let tmp = TempDir::new("player-probe");
    let body = format!("printf '%s\\n' '{}'", line("Playing", 61_000_000, "Song"));
    let s = probe_locked(&fake(&tmp, &body)).unwrap();
    assert_eq!(args(&tmp), ["metadata", "--format", FORMAT]);
    assert_eq!((s.instance.as_str(), s.media.position_s, s.media.title.as_str()), ("spotify", 61, "Song"));

    let none = TempDir::new("player-probe-none");
    let s = probe_locked(&fake(&none, "echo 'No players found' >&2\nexit 1")).unwrap();
    assert_eq!(s, Snapshot::default());

    assert_eq!(probe_locked(Path::new("/nonexistent/playerctl")).unwrap_err().kind(), ErrorKind::NotFound);
    assert_eq!(spawn(Path::new("/nonexistent/playerctl")).err().unwrap().kind(), ErrorKind::NotFound);
}
