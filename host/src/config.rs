//! nyxd configuration: ~/.config/nyxd/config.toml (see config.example.toml).

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::Deserialize;

use crate::metrics::{Gpu, NET_EXCLUDE};

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    /// Seconds between metrics updates (the device drops the link after 5 s).
    pub interval: f64,
    /// A fixed /dev/hidrawN instead of auto-discovery.
    pub device: Option<String>,
    /// The file system the DISK bar shows.
    pub disk_path: PathBuf,
    pub gpu: Gpu,
    pub net_exclude: Vec<String>,
    /// HOST(id) -> shell command, run on press.
    pub keys: BTreeMap<u8, String>,
    /// Text slots shown at start-up.
    pub text: BTreeMap<usize, String>,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            interval: 1.0,
            device: None,
            disk_path: "/".into(),
            gpu: Gpu::Auto,
            net_exclude: NET_EXCLUDE.iter().map(|s| s.to_string()).collect(),
            keys: BTreeMap::new(),
            text: BTreeMap::new(),
        }
    }
}

#[derive(Deserialize, Default)]
#[serde(deny_unknown_fields)]
struct General {
    interval: Option<f64>,
    device: Option<String>,
    disk_path: Option<PathBuf>,
    gpu: Option<String>,
    net_exclude: Option<Vec<String>>,
}

#[derive(Deserialize, Default)]
#[serde(deny_unknown_fields)]
struct FileFormat {
    #[serde(default)]
    general: General,
    #[serde(default)]
    keys: BTreeMap<String, String>,
    #[serde(default)]
    text: BTreeMap<String, String>,
}

pub fn default_path() -> PathBuf {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))
        .unwrap_or_else(|| PathBuf::from("."));
    base.join("nyxd/config.toml")
}

/// The configuration at `path`; a missing file means the defaults.
pub fn load(path: &Path) -> Result<Config, String> {
    match std::fs::read_to_string(path) {
        Ok(text) => parse(&text).map_err(|e| format!("{}: {e}", path.display())),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Config::default()),
        Err(e) => Err(format!("{}: {e}", path.display())),
    }
}

pub fn parse(text: &str) -> Result<Config, String> {
    let f: FileFormat = toml::from_str(text).map_err(|e| e.to_string())?;
    let mut cfg = Config::default();
    let g = f.general;
    if let Some(i) = g.interval {
        if !(0.2..=4.0).contains(&i) {
            return Err("interval must be 0.2 .. 4 s (the macropad drops the link after 5 s)".into());
        }
        cfg.interval = i;
    }
    cfg.device = g.device;
    if let Some(p) = g.disk_path {
        cfg.disk_path = p;
    }
    if let Some(gpu) = g.gpu {
        cfg.gpu = match gpu.as_str() {
            "auto" => Gpu::Auto,
            "amd" => Gpu::Amd,
            "nvidia" => Gpu::Nvidia,
            "off" => Gpu::Off,
            _ => return Err("gpu must be auto, amd, nvidia or off".into()),
        };
    }
    if let Some(n) = g.net_exclude {
        cfg.net_exclude = n;
    }
    for (k, v) in f.keys {
        let id: u8 = k.parse().map_err(|_| format!("[keys] {k:?}: the key must be a HOST id 0..255"))?;
        cfg.keys.insert(id, v);
    }
    for (k, v) in f.text {
        let slot: usize = k.parse().ok().filter(|s| *s < 4).ok_or(format!("[text] {k:?}: slots are 0..3"))?;
        cfg.text.insert(slot, v);
    }
    Ok(cfg)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn example_config_parses() {
        let text = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/config.example.toml")).unwrap();
        assert_eq!(parse(&text).unwrap(), Config::default()); // everything in it is commented out or a default
    }

    #[test]
    fn values_and_errors() {
        let cfg = parse(
            "[general]\ninterval = 2.0\ngpu = \"off\"\n[keys]\n1 = \"playerctl play-pause\"\n[text]\n0 = \"hi\"\n",
        )
        .unwrap();
        assert_eq!(cfg.interval, 2.0);
        assert_eq!(cfg.gpu, Gpu::Off);
        assert_eq!(cfg.keys[&1], "playerctl play-pause");
        assert_eq!(cfg.text[&0], "hi");
        assert!(parse("[general]\ninterval = 9.0\n").is_err());
        assert!(parse("[general]\ngpu = \"intel\"\n").is_err());
        assert!(parse("[keys]\nx = \"a\"\n").is_err());
        assert!(parse("[text]\n7 = \"a\"\n").is_err());
        assert!(parse("[general]\nintervall = 1.0\n").is_err()); // typos are reported, not ignored
    }
}
