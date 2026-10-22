//! nyxctl: control a running nyxd (status, text on the bar display).

use std::path::PathBuf;
use std::process::ExitCode;

use nyxd::daemon::{control_path, request};
use serde_json::{json, Value};

const USAGE: &str = "usage: nyxctl [--socket PATH] <command>

  status               connection state, firmware, edition, current text
  text SLOT TEXT...    show text in slot 0..3 on the bar display (until replaced)
  clear [SLOT]         clear one text slot, or all";

fn main() -> ExitCode {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut socket: PathBuf = control_path();
    if args.first().map(String::as_str) == Some("--socket") {
        if args.len() < 2 {
            eprintln!("nyxctl: --socket needs a value");
            return ExitCode::from(2);
        }
        socket = args[1].clone().into();
        args.drain(..2);
    }
    let slot = |s: &str| s.parse::<u64>().map_err(|_| format!("not a slot number: {s}"));
    let req: Result<Value, String> = match args.first().map(String::as_str) {
        Some("status") if args.len() == 1 => Ok(json!({"cmd": "status"})),
        Some("text") if args.len() >= 3 => {
            slot(&args[1]).map(|s| json!({"cmd": "text", "slot": s, "text": args[2..].join(" ")}))
        }
        Some("clear") if args.len() == 1 => Ok(json!({"cmd": "clear"})),
        Some("clear") if args.len() == 2 => slot(&args[1]).map(|s| json!({"cmd": "clear", "slot": s})),
        Some("-h" | "--help") => {
            println!("{USAGE}");
            return ExitCode::SUCCESS;
        }
        _ => Err(USAGE.to_string()),
    };
    let req = match req {
        Ok(r) => r,
        Err(e) => {
            eprintln!("nyxctl: {e}");
            return ExitCode::from(2);
        }
    };
    let reply = match request(&req, &socket) {
        Ok(r) => r,
        Err(e) => {
            eprintln!("nyxctl: cannot reach nyxd at {}: {e}", socket.display());
            return ExitCode::from(2);
        }
    };
    if reply["ok"] != json!(true) {
        eprintln!("nyxctl: {}", reply["error"].as_str().unwrap_or("request failed"));
        return ExitCode::from(1);
    }
    if req["cmd"] == "status" {
        if let Some(map) = reply.as_object() {
            for (k, v) in map.iter().filter(|(k, _)| k.as_str() != "ok") {
                println!("{k:18} {v}");
            }
        }
    }
    ExitCode::SUCCESS
}
