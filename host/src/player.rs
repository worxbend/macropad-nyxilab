//! MPRIS media players for the PLAYER page, and the HOST keys that control them.
//!
//! Through the `playerctl` command, not D-Bus: one `playerctl --follow` child prints a line
//! whenever the most recently active player changes.  With `{{position}}` in its format it also
//! prints once a second while playing, so the position (and a seek) shows within a second
//! without polling.  When the last player goes away it prints an empty line.

use std::io::{self, Read};
use std::os::fd::{AsRawFd, RawFd};
use std::path::Path;
use std::process::{Child, ChildStdout, Command, Stdio};

use crate::protocol::{Media, Status};

/// HOST(id) keys 211..=218 control the player (unless [keys] maps the id to a command).
pub const KEY_BASE: u8 = 210;
const SEP: char = '\x1f';
/// playerInstance first: the commands name it with --player, so they reach the player shown.
pub const FORMAT: &str = "{{playerInstance}}\x1f{{playerName}}\x1f{{status}}\x1f{{position}}\x1f{{mpris:length}}\
                          \x1f{{volume}}\x1f{{title}}\x1f{{artist}}\x1f{{album}}";
/// A line this long without a newline is not playerctl's.
const MAX_LINE: usize = 64 << 10;

/// One playerctl line: the player's instance name (for --player) and what it shows.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Snapshot {
    pub instance: String,
    pub media: Media,
}

/// A line of FORMAT; an empty line means no player.  None = not a line of ours.
pub fn parse_line(line: &str) -> Option<Snapshot> {
    if line.is_empty() {
        return Some(Snapshot::default());
    }
    let f: Vec<&str> = line.split(SEP).collect();
    let [instance, name, status, position, length, volume, title, artist, album] = f[..] else { return None };
    let status = match status {
        "Playing" => Status::Playing,
        "Paused" => Status::Paused,
        _ => Status::Stopped,
    };
    let media = Media {
        present: true,
        status,
        volume: volume.parse::<f64>().ok().map(|v| (v * 100.0).round().clamp(0.0, 100.0) as u8),
        position_s: micros_to_s(position),
        length_s: micros_to_s(length),
        name: name.to_string(),
        title: title.to_string(),
        artist: artist.to_string(),
        album: album.to_string(),
    };
    Some(Snapshot { instance: instance.to_string(), media })
}

/// playerctl prints position and mpris:length in microseconds; empty = unknown = 0.
fn micros_to_s(text: &str) -> u32 {
    text.parse::<i64>().map_or(0, |us| (us / 1_000_000).clamp(0, i64::from(u32::MAX)) as u32)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    PlayPause,
    Next,
    Previous,
    Stop,
    SeekBack,
    SeekForward,
    VolumeDown,
    VolumeUp,
}

impl Action {
    /// The action of a HOST key, in the order of the PLAYER page's keys.
    pub fn from_key(id: u8) -> Option<Action> {
        use Action::*;
        let all = [PlayPause, Next, Previous, Stop, SeekBack, SeekForward, VolumeDown, VolumeUp];
        let n = id.checked_sub(KEY_BASE + 1)?;
        all.get(usize::from(n)).copied()
    }

    fn args(self) -> &'static [&'static str] {
        match self {
            Action::PlayPause => &["play-pause"],
            Action::Next => &["next"],
            Action::Previous => &["previous"],
            Action::Stop => &["stop"],
            Action::SeekBack => &["position", "10-"],
            Action::SeekForward => &["position", "10+"],
            Action::VolumeDown => &["volume", "0.05-"],
            Action::VolumeUp => &["volume", "0.05+"],
        }
    }
}

/// The playerctl command for `action` on the player `instance` (argv, no shell).
pub fn command(program: &Path, instance: &str, action: Action) -> Command {
    let mut cmd = Command::new(program);
    if !instance.is_empty() {
        cmd.args(["--player", instance]);
    }
    cmd.args(action.args());
    cmd
}

/// The current player once, for `nyxd --probe`; no player = the default snapshot.
pub fn probe(program: &Path) -> io::Result<Snapshot> {
    let out = Command::new(program).args(["metadata", "--format", FORMAT]).stderr(Stdio::null()).output()?;
    if !out.status.success() {
        return Ok(Snapshot::default()); // "No players found"
    }
    let text = String::from_utf8_lossy(&out.stdout);
    Ok(parse_line(text.trim_end_matches(['\n', '\r'])).unwrap_or_default())
}

/// A running `playerctl --follow`.
pub struct Follower {
    child: Child,
    out: ChildStdout,
    buf: Vec<u8>,
}

impl Follower {
    pub fn spawn(program: &Path) -> io::Result<Self> {
        let mut child = Command::new(program)
            .args(["--follow", "metadata", "--format", FORMAT])
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()?;
        let out = child.stdout.take().expect("stdout is piped");
        let fd = out.as_raw_fd();
        let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
        if flags < 0 || unsafe { libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) } < 0 {
            let e = io::Error::last_os_error();
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
        Ok(Follower { child, out, buf: Vec::new() })
    }

    /// Its stdout, for poll(2).
    pub fn fd(&self) -> RawFd {
        self.out.as_raw_fd()
    }

    /// The newest complete line's snapshot, if one arrived; an error once playerctl has exited.
    pub fn read(&mut self) -> io::Result<Option<Snapshot>> {
        let mut latest = None;
        let mut chunk = [0u8; 4096];
        loop {
            match self.out.read(&mut chunk) {
                Ok(0) if latest.is_some() => return Ok(latest), // the next read reports the exit
                Ok(0) => return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "playerctl exited")),
                Ok(n) => {
                    self.buf.extend_from_slice(&chunk[..n]);
                    while let Some(end) = self.buf.iter().position(|&b| b == b'\n') {
                        let line: Vec<u8> = self.buf.drain(..=end).collect();
                        let text = String::from_utf8_lossy(&line[..end]);
                        if let Some(s) = parse_line(text.trim_end_matches('\r')) {
                            latest = Some(s);
                        }
                    }
                    if self.buf.len() > MAX_LINE {
                        self.buf.clear();
                    }
                }
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => return Ok(latest),
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(e) => return Err(e),
            }
        }
    }
}

impl Drop for Follower {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn line(fields: [&str; 9]) -> String {
        fields.join("\x1f")
    }

    #[test]
    fn format_has_one_field_per_parse_slot() {
        assert_eq!(FORMAT.split(SEP).count(), 9);
        assert!(!FORMAT.contains(' '));
    }

    #[test]
    fn a_playerctl_line_becomes_a_snapshot() {
        let l = line(["spotify", "spotify", "Playing", "123496021", "131613000", "0.5", "Song", "A, B", "LP"]);
        let s = parse_line(&l).unwrap();
        assert_eq!(s.instance, "spotify");
        assert_eq!(
            s.media,
            Media {
                present: true,
                status: Status::Playing,
                volume: Some(50),
                position_s: 123,
                length_s: 131,
                name: "spotify".into(),
                title: "Song".into(),
                artist: "A, B".into(),
                album: "LP".into(),
            }
        );
    }

    #[test]
    fn odd_values_are_unknown_not_errors() {
        // firefox with a live stream: length i64::MAX-ish, no album, a volume above 1
        let l = line(["firefox.instance_1_52", "firefox", "Paused", "", "9223372036854000000", "1.7", "t", "", ""]);
        let m = parse_line(&l).unwrap().media;
        assert_eq!((m.status, m.position_s, m.length_s, m.volume), (Status::Paused, 0, u32::MAX, Some(100)));
        let l = line(["p", "p", "Stopped", "-5", "0", "", "", "", ""]);
        let m = parse_line(&l).unwrap().media;
        assert_eq!((m.status, m.position_s, m.volume), (Status::Stopped, 0, None));
        assert_eq!(parse_line("").unwrap(), Snapshot::default()); // the last player went away
        assert_eq!(parse_line("No players found"), None);
    }

    #[test]
    fn keys_211_to_218_are_the_player_actions() {
        assert_eq!(Action::from_key(210), None);
        assert_eq!(Action::from_key(211), Some(Action::PlayPause));
        assert_eq!(Action::from_key(214), Some(Action::Stop));
        assert_eq!(Action::from_key(218), Some(Action::VolumeUp));
        assert_eq!(Action::from_key(219), None);
        assert_eq!(Action::from_key(3), None);
    }

    #[test]
    fn commands_are_argv_for_the_shown_player() {
        let args = |cmd: &Command| cmd.get_args().map(|a| a.to_string_lossy().into_owned()).collect::<Vec<_>>();
        let cmd = command(Path::new("playerctl"), "firefox.instance_1_52", Action::SeekBack);
        assert_eq!(cmd.get_program(), "playerctl");
        assert_eq!(args(&cmd), ["--player", "firefox.instance_1_52", "position", "10-"]);
        assert_eq!(args(&command(Path::new("p"), "", Action::VolumeUp)), ["volume", "0.05+"]);
        assert_eq!(args(&command(Path::new("p"), "s", Action::Next)), ["--player", "s", "next"]);
    }
}
