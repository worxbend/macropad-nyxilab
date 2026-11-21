//! sway: the workspaces and the focused window for the HOST dashboard, and the HOST keys that
//! switch workspaces.
//!
//! Talks to sway's IPC socket (the i3 protocol) directly: one connection subscribed to workspace
//! and window events, one for queries and commands.  Messages are "i3-ipc", a u32 payload length,
//! a u32 type (native byte order) and a JSON payload.

use std::io::{self, Read, Write};
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::time::Duration;

use serde_json::Value;

use crate::protocol::{Layout, Workspaces, WORKSPACE_KEYS};

const MAGIC: &[u8; 6] = b"i3-ipc";
const RUN_COMMAND: u32 = 0;
const GET_WORKSPACES: u32 = 1;
const SUBSCRIBE: u32 = 2;
const GET_TREE: u32 = 4;
/// A tree of a busy desktop is a few hundred KiB; anything this big is not sway.
const MAX_PAYLOAD: u32 = 16 << 20;

/// HOST(id) keys 201..=208 switch to workspaces 1..=8 (unless [keys] maps the id to a command).
pub const WORKSPACE_KEY_BASE: u8 = 200;

/// The workspace a HOST key switches to.
pub fn workspace_key(id: u8) -> Option<u8> {
    let n = id.checked_sub(WORKSPACE_KEY_BASE)?;
    (1..=WORKSPACE_KEYS).contains(&n).then_some(n)
}

/// $SWAYSOCK, else sway's socket in $XDG_RUNTIME_DIR (a systemd user service does not get
/// SWAYSOCK unless the sway config imports it): sway-ipc.<uid>.<pid>.sock, the newest one.
pub fn socket_path() -> Option<PathBuf> {
    if let Some(p) = std::env::var_os("SWAYSOCK") {
        return Some(p.into());
    }
    let dir = PathBuf::from(std::env::var_os("XDG_RUNTIME_DIR")?);
    let prefix = format!("sway-ipc.{}.", unsafe { libc::getuid() });
    std::fs::read_dir(dir)
        .ok()?
        .filter_map(Result::ok)
        .filter(|e| e.file_name().to_str().is_some_and(|n| n.starts_with(&prefix) && n.ends_with(".sock")))
        .max_by_key(|e| e.metadata().and_then(|m| m.modified()).ok())
        .map(|e| e.path())
}

pub fn encode(kind: u32, payload: &[u8]) -> Vec<u8> {
    let mut msg = Vec::with_capacity(14 + payload.len());
    msg.extend_from_slice(MAGIC);
    msg.extend_from_slice(&(payload.len() as u32).to_ne_bytes());
    msg.extend_from_slice(&kind.to_ne_bytes());
    msg.extend_from_slice(payload);
    msg
}

/// One message: (type, JSON payload).
pub fn read_message(r: &mut impl Read) -> io::Result<(u32, Value)> {
    let mut head = [0u8; 14];
    r.read_exact(&mut head)?;
    if &head[..6] != MAGIC {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "not an i3-ipc message"));
    }
    let len = u32::from_ne_bytes(head[6..10].try_into().expect("4 bytes"));
    let kind = u32::from_ne_bytes(head[10..14].try_into().expect("4 bytes"));
    if len > MAX_PAYLOAD {
        return Err(io::Error::new(io::ErrorKind::InvalidData, format!("i3-ipc payload of {len} bytes")));
    }
    let mut payload = vec![0u8; len as usize];
    r.read_exact(&mut payload)?;
    let value = serde_json::from_slice(&payload).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
    Ok((kind, value))
}

/// The dashboard's view of GET_WORKSPACES and GET_TREE replies.
pub fn state(workspaces: &Value, tree: &Value) -> Workspaces {
    let mut w = Workspaces { running: true, ..Workspaces::default() };
    if let Some(window) = focused_window(tree) {
        w.title = text(window, "name");
        w.app = app(window);
    }
    for ws in workspaces.as_array().into_iter().flatten() {
        let flag = |key: &str| ws.get(key).and_then(Value::as_bool).unwrap_or(false);
        if flag("focused") {
            w.layout = layout(ws.get("layout").and_then(Value::as_str).unwrap_or_default());
            let name = ws.get("name").and_then(Value::as_str).unwrap_or_default();
            w.windows = find_workspace(tree, name).map_or(0, count_windows).min(255) as u8;
        }
        let num = ws.get("num").and_then(Value::as_i64).unwrap_or(-1); // -1: a named workspace
        if !(1..=i64::from(WORKSPACE_KEYS)).contains(&num) {
            continue;
        }
        let bit = 1u8 << (num - 1);
        w.open |= bit;
        if flag("visible") {
            w.visible |= bit;
        }
        if flag("urgent") {
            w.urgent |= bit;
        }
        if flag("focused") {
            w.focused = num as u8;
        }
    }
    w
}

pub fn layout(name: &str) -> Layout {
    match name {
        "splith" => Layout::SplitH,
        "splitv" => Layout::SplitV,
        "tabbed" => Layout::Tabbed,
        "stacked" => Layout::Stacked,
        _ => Layout::Unknown,
    }
}

fn text(node: &Value, key: &str) -> String {
    node.get(key).and_then(Value::as_str).unwrap_or_default().to_string()
}

fn children(node: &Value) -> impl Iterator<Item = &Value> {
    ["nodes", "floating_nodes"].into_iter().filter_map(|key| node.get(key).and_then(Value::as_array)).flatten()
}

fn node_type(node: &Value) -> &str {
    node.get("type").and_then(Value::as_str).unwrap_or_default()
}

/// The focused window; None when an empty workspace has the focus.
fn focused_window(node: &Value) -> Option<&Value> {
    if node.get("focused").and_then(Value::as_bool) == Some(true) {
        return (!matches!(node_type(node), "workspace" | "output" | "root")).then_some(node);
    }
    children(node).find_map(focused_window)
}

/// The window's app as a readable name (`app_name`): Wayland windows have an app_id, XWayland ones a class.
fn app(window: &Value) -> String {
    match window.get("app_id").and_then(Value::as_str) {
        Some(id) => app_name(id),
        None => app_name(&window.get("window_properties").map(|p| text(p, "class")).unwrap_or_default()),
    }
}

/// "org.telegram.telegram-desktop" -> "Telegram Desktop": the part after the last dot (a Flatpak id's app
/// name), its words split at '-' and '_', each capitalised.
pub fn app_name(id: &str) -> String {
    let last = id.rsplit('.').next().filter(|s| !s.is_empty()).unwrap_or(id);
    let capitalised = |word: &str| {
        let mut chars = word.chars();
        chars.next().map(|first| first.to_uppercase().chain(chars).collect::<String>()).unwrap_or_default()
    };
    last.split(['-', '_']).filter(|w| !w.is_empty()).map(capitalised).collect::<Vec<_>>().join(" ")
}

fn find_workspace<'a>(node: &'a Value, name: &str) -> Option<&'a Value> {
    if node_type(node) == "workspace" && node.get("name").and_then(Value::as_str) == Some(name) {
        return Some(node);
    }
    children(node).find_map(|c| find_workspace(c, name))
}

/// Windows are the leaves; the cons above them only split the space.
fn count_windows(node: &Value) -> usize {
    let is_con = matches!(node_type(node), "con" | "floating_con");
    match children(node).map(count_windows).sum() {
        0 if is_con => 1,
        n => n,
    }
}

/// A connection to a running sway.
pub struct Sway {
    events: UnixStream,
    requests: UnixStream,
}

impl Sway {
    pub fn connect(path: &Path) -> io::Result<Self> {
        let open = || -> io::Result<UnixStream> {
            let s = UnixStream::connect(path)?;
            s.set_read_timeout(Some(Duration::from_secs(1)))?;
            s.set_write_timeout(Some(Duration::from_secs(1)))?;
            Ok(s)
        };
        let mut sway = Sway { events: open()?, requests: open()? };
        sway.events.write_all(&encode(SUBSCRIBE, br#"["workspace","window"]"#))?;
        let (_, reply) = read_message(&mut sway.events)?;
        if reply.get("success").and_then(Value::as_bool) != Some(true) {
            return Err(io::Error::other(format!("subscribe refused: {reply}")));
        }
        Ok(sway)
    }

    /// The event connection, for poll(2).
    pub fn fd(&self) -> RawFd {
        self.events.as_raw_fd()
    }

    /// Takes one event off the event connection (what it says does not matter: query() reads the state).
    pub fn take_event(&mut self) -> io::Result<()> {
        read_message(&mut self.events).map(drop)
    }

    fn request(&mut self, kind: u32, payload: &[u8]) -> io::Result<Value> {
        self.requests.write_all(&encode(kind, payload))?;
        loop {
            let (got, value) = read_message(&mut self.requests)?;
            if got == kind {
                return Ok(value);
            }
        }
    }

    pub fn query(&mut self) -> io::Result<Workspaces> {
        let workspaces = self.request(GET_WORKSPACES, b"")?;
        let tree = self.request(GET_TREE, b"")?;
        Ok(state(&workspaces, &tree))
    }

    pub fn switch_to(&mut self, workspace: u8) -> io::Result<()> {
        let reply = self.request(RUN_COMMAND, format!("workspace number {workspace}").as_bytes())?;
        match reply.get(0).and_then(|r| r.get("success")).and_then(Value::as_bool) {
            Some(true) => Ok(()),
            _ => Err(io::Error::other(format!("sway refused the switch: {reply}"))),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn workspace_keys_are_201_to_208() {
        assert_eq!(workspace_key(200), None);
        assert_eq!(workspace_key(201), Some(1));
        assert_eq!(workspace_key(208), Some(8));
        assert_eq!(workspace_key(209), None);
        assert_eq!(workspace_key(3), None);
    }

    #[test]
    fn layout_names() {
        assert_eq!(layout("splith"), Layout::SplitH);
        assert_eq!(layout("splitv"), Layout::SplitV);
        assert_eq!(layout("tabbed"), Layout::Tabbed);
        assert_eq!(layout("stacked"), Layout::Stacked);
        assert_eq!(layout("output"), Layout::Unknown);
    }

    #[test]
    fn app_id_of_a_wayland_window_wins() {
        let w = json!({"type": "root", "nodes": [{"type": "con", "name": "t", "app_id": "foot",
            "window_properties": {"class": "x"}, "focused": true}]});
        assert_eq!(state(&json!([]), &w).app, "Foot");
    }

    #[test]
    fn app_names_are_readable() {
        assert_eq!(app_name("org.telegram.telegram-desktop"), "Telegram Desktop");
        assert_eq!(app_name("org.gnome.Nautilus"), "Nautilus");
        assert_eq!(app_name("com.github.tchx84.Flatseal"), "Flatseal");
        assert_eq!(app_name("firefox"), "Firefox");
        assert_eq!(app_name("jetbrains-idea_ce"), "Jetbrains Idea Ce");
        assert_eq!(app_name("ärger"), "Ärger");
        assert_eq!(app_name(""), "");
        assert_eq!(app_name("trailing."), "Trailing.");
    }

    #[test]
    fn messages_round_trip_and_bad_ones_are_refused() {
        let msg = encode(GET_TREE, br#"{"a":1}"#);
        assert_eq!(&msg[..6], b"i3-ipc");
        assert_eq!(read_message(&mut msg.as_slice()).unwrap(), (GET_TREE, json!({"a": 1})));
        let mut bad = msg.clone();
        bad[0] = b'x';
        assert!(read_message(&mut bad.as_slice()).is_err());
        let mut huge = encode(0, b"");
        huge[6..10].copy_from_slice(&(MAX_PAYLOAD + 1).to_ne_bytes());
        assert!(read_message(&mut huge.as_slice()).is_err()); // no 16 MiB allocation for a bad header
        assert!(read_message(&mut &msg[..10]).is_err()); // cut short
    }

    #[test]
    fn state_maps_workspaces_1_to_8_and_finds_the_focused_window() {
        let workspaces = json!([
            {"num": 1, "name": "1", "focused": false, "visible": false, "urgent": false},
            {"num": 2, "name": "2", "focused": false, "visible": false, "urgent": true},
            {"num": 3, "name": "3: web", "focused": true, "visible": true, "urgent": false, "layout": "tabbed"},
            {"num": 5, "name": "5", "focused": false, "visible": true, "urgent": false},
            {"num": 9, "name": "9", "focused": false, "visible": false, "urgent": false},
            {"num": -1, "name": "music", "focused": false, "visible": false, "urgent": false}
        ]);
        let tree = json!({"type": "root", "focused": false, "nodes": [
            {"type": "output", "focused": false, "nodes": [
                {"type": "workspace", "name": "1", "focused": false, "nodes": [
                    {"type": "con", "name": "elsewhere", "focused": false, "nodes": []}
                ]},
                {"type": "workspace", "name": "3: web", "focused": false, "nodes": [
                    {"type": "con", "name": "terminal", "app_id": "foot", "focused": false, "nodes": []},
                    {"type": "con", "name": null, "layout": "splitv", "focused": false, "nodes": [
                        {"type": "con", "name": "a", "focused": false, "nodes": []},
                        {"type": "con", "name": "b", "focused": false, "nodes": []}
                    ]}
                ], "floating_nodes": [
                    {"type": "floating_con", "name": "Firefox", "app_id": null,
                     "window_properties": {"class": "firefox-esr"}, "focused": true, "nodes": []}
                ]}
            ]}
        ]});
        let w = state(&workspaces, &tree);
        assert_eq!(
            w,
            Workspaces {
                running: true,
                focused: 3,
                open: 0x17,
                visible: 0x14,
                urgent: 0x02,
                windows: 4, // terminal, a, b and the floating Firefox; not the split con
                layout: Layout::Tabbed,
                app: "Firefox Esr".into(), // XWayland: the class
                title: "Firefox".into(),
            }
        );
        let empty = json!({"type": "root", "nodes": [{"type": "workspace", "name": "4", "focused": true}]});
        assert_eq!(state(&json!([{"num": 9, "focused": true}]), &empty).title, ""); // an empty workspace
        assert_eq!(state(&json!([{"num": 9, "focused": true}]), &empty).focused, 0); // outside the keys
        assert_eq!(state(&json!([{"num": 9, "focused": true}]), &empty).windows, 0);
        assert_eq!(state(&json!("garbage"), &json!(null)), Workspaces { running: true, ..Workspaces::default() });
    }
}
