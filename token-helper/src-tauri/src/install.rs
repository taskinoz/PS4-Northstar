//! Installing PS4 Northstar into a game folder, updating it and removing it.
//!
//! What an install puts in the game folder (the folder with the game's
//! eboot.bin), from the newest PS4 Northstar release on GitHub:
//! - R2Northstar/mods: Northstar's own mods (from a `northstar-mods-*.zip`
//!   asset of the release, or else from Northstar's own release zip), this
//!   port's mods (`northstar-ps4-mods-*.zip`) and Northstar.Custom's converted
//!   paks (`northstar-custom-ps4-rpaks-*.zip`);
//! - bin/ps4_retail/northstar_ps4.prx, the runtime;
//! - the bootstrap in eboot.bin that loads it, as scripts/Enable-Stage2Bootstrap.ps1
//!   adds it, with the original kept as eboot.bin.northstar-stage2.bak;
//! - R2Northstar/ps4-northstar-install.json, what was installed.
//!
//! Every download is checked against the SHA-256 GitHub publishes for it.
//! The game's archives are never touched, and other mods in R2Northstar/mods
//! are left alone.

use serde::Serialize;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::fs;
use std::io::{Read, Write};
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

pub const VANILLA_EBOOT_SHA256: &str = "590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824";
pub const PATCHED_EBOOT_SHA256: &str = "5a4b52ff7224cca7bb9f5f25888847d5d2bd1669af1a4f21328927932fbfd502";
pub const RELEASES_API: &str = "https://api.github.com/repos/taskinoz/PS4-Northstar/releases?per_page=20";
/// Northstar's own release, used for its mods when a PS4 Northstar release has no mods zip.
pub const NORTHSTAR_ZIP_URL: &str =
    "https://github.com/R2Northstar/Northstar/releases/download/v1.31.13/Northstar.release.v1.31.13.zip";
pub const NORTHSTAR_ZIP_SHA256: &str = "e622b96e7609912060a61ba3eed382eeafd6cc7b62c105ea609a36bc36e75322";
const NORTHSTAR_MODS: [&str; 3] = ["Northstar.Client", "Northstar.Custom", "Northstar.CustomServers"];
const PS4_MODS: [&str; 2] = ["Northstar.PS4", "Northstar.DirectConnect"];
const BACKUP_NAME: &str = "eboot.bin.northstar-stage2.bak";
const RECORD: &str = "R2Northstar/ps4-northstar-install.json";
const PRX: &str = "bin/ps4_retail/northstar_ps4.prx";

/// Where releases come from; tests point these at a local server.
#[derive(Clone, Debug)]
pub struct Sources {
    pub releases_api: String,
    pub northstar_zip_url: String,
    pub northstar_zip_sha256: String,
    pub vanilla_eboot_sha256: String,
    pub patched_eboot_sha256: String,
}

impl Default for Sources {
    fn default() -> Self {
        Sources {
            releases_api: RELEASES_API.into(),
            northstar_zip_url: NORTHSTAR_ZIP_URL.into(),
            northstar_zip_sha256: NORTHSTAR_ZIP_SHA256.into(),
            vanilla_eboot_sha256: VANILLA_EBOOT_SHA256.into(),
            patched_eboot_sha256: PATCHED_EBOOT_SHA256.into(),
        }
    }
}

fn sha256_hex(data: &[u8]) -> String {
    hex::encode(Sha256::digest(data))
}

// ---- The eboot bootstrap ----

const CALL_SITE: usize = 0x528d; // file offset of `call 0x20` at virtual address 0x128d
const ORIGINAL_CALL: [u8; 5] = [0xe8, 0x8e, 0xed, 0xff, 0xff];
const STUB_VA: u64 = 0x29e0;
const RX_SEGMENT_SIZE: u64 = 0x29dc;

fn u16_at(data: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_le_bytes(data.get(at..at + 2)?.try_into().ok()?))
}
fn u32_at(data: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(data.get(at..at + 4)?.try_into().ok()?))
}
fn u64_at(data: &[u8], at: usize) -> Option<u64> {
    Some(u64::from_le_bytes(data.get(at..at + 8)?.try_into().ok()?))
}

/// The program header of the executable PT_LOAD at file offset 0x4000.
fn rx_program_header(data: &[u8]) -> Option<usize> {
    let phoff = u64_at(data, 0x20)? as usize;
    let size = u16_at(data, 0x36)? as usize;
    let count = u16_at(data, 0x38)? as usize;
    (0..count).map(|i| phoff + i * size).find(|&ph| {
        u32_at(data, ph) == Some(1) && u32_at(data, ph + 4) == Some(5) && u64_at(data, ph + 8) == Some(0x4000)
    })
}

/// The bootstrap: a stub in the gap after the executable segment that calls
/// the original target and then loads /app0/bin/ps4_retail/northstar_ps4.prx
/// through the loader PLT entry at 0x14e0; the call at 0x128d is pointed at it.
fn bootstrap_payload() -> Vec<u8> {
    let mut code: Vec<u8> = vec![0x48, 0x83, 0xec, 0x08]; // sub rsp, 8
    let call_init_next = STUB_VA as i64 + code.len() as i64 + 5;
    code.push(0xe8);
    code.extend_from_slice(&((0x20 - call_init_next) as i32).to_le_bytes());
    let lea_start = STUB_VA as i64 + code.len() as i64;
    let path_va = STUB_VA as i64 + 38;
    code.extend_from_slice(&[0x48, 0x8d, 0x3d]); // lea rdi, [rip + path]
    code.extend_from_slice(&((path_va - (lea_start + 7)) as i32).to_le_bytes());
    code.extend_from_slice(&[0x31, 0xf6, 0x31, 0xd2, 0x31, 0xc9, 0x45, 0x31, 0xc0, 0x45, 0x31, 0xc9]);
    let call_loader_next = STUB_VA as i64 + code.len() as i64 + 5;
    code.push(0xe8);
    code.extend_from_slice(&((0x14e0 - call_loader_next) as i32).to_le_bytes());
    code.extend_from_slice(&[0x48, 0x83, 0xc4, 0x08, 0xc3]); // add rsp, 8; ret
    debug_assert_eq!(code.len(), 38);
    code.extend_from_slice(b"/app0/bin/ps4_retail/northstar_ps4.prx\0");
    code
}

/// Adds the bootstrap to an original eboot. Checks every byte it relies on.
pub fn patch_eboot(original: &[u8]) -> Result<Vec<u8>, String> {
    let fail = |what: &str| format!("eboot.bin is not the supported game version ({what})");
    if original.get(CALL_SITE..CALL_SITE + 5) != Some(&ORIGINAL_CALL[..]) {
        return Err(fail("unexpected startup code"));
    }
    let ph = rx_program_header(original).ok_or_else(|| fail("no executable segment"))?;
    if u64_at(original, ph + 0x20) != Some(RX_SEGMENT_SIZE) || u64_at(original, ph + 0x28) != Some(RX_SEGMENT_SIZE) {
        return Err(fail("unexpected segment size"));
    }
    let payload = bootstrap_payload();
    let stub = 0x4000 + STUB_VA as usize;
    match original.get(stub..stub + payload.len()) {
        Some(gap) if gap.iter().all(|&b| b == 0) => {}
        _ => return Err(fail("no room for the bootstrap")),
    }
    let mut patched = original.to_vec();
    patched[stub..stub + payload.len()].copy_from_slice(&payload);
    let size = STUB_VA + payload.len() as u64;
    patched[ph + 0x20..ph + 0x28].copy_from_slice(&size.to_le_bytes());
    patched[ph + 0x28..ph + 0x30].copy_from_slice(&size.to_le_bytes());
    let call_next = 0x128d + 5;
    patched[CALL_SITE] = 0xe8;
    patched[CALL_SITE + 1..CALL_SITE + 5].copy_from_slice(&((STUB_VA as i64 - call_next) as i32).to_le_bytes());
    Ok(patched)
}

/// Takes the bootstrap out again: the exact reverse of patch_eboot.
pub fn unpatch_eboot(patched: &[u8]) -> Result<Vec<u8>, String> {
    let payload = bootstrap_payload();
    let stub = 0x4000 + STUB_VA as usize;
    let ph = rx_program_header(patched).ok_or("eboot.bin has no executable segment")?;
    if patched.get(stub..stub + payload.len()) != Some(&payload[..]) {
        return Err("eboot.bin does not have this bootstrap".into());
    }
    let mut original = patched.to_vec();
    original[stub..stub + payload.len()].fill(0);
    original[ph + 0x20..ph + 0x28].copy_from_slice(&RX_SEGMENT_SIZE.to_le_bytes());
    original[ph + 0x28..ph + 0x30].copy_from_slice(&RX_SEGMENT_SIZE.to_le_bytes());
    original[CALL_SITE..CALL_SITE + 5].copy_from_slice(&ORIGINAL_CALL);
    Ok(original)
}

// ---- The game folder ----

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Eboot {
    Missing,
    Original,
    Bootstrapped,
    Unknown,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderInfo {
    pub folder: String,
    pub eboot: Eboot,
    pub has_archives: bool,
    pub runtime_installed: bool,
    pub installed_version: Option<String>,
    pub mod_count: usize,
}

impl FolderInfo {
    /// A sentence about the folder for the window and the terminal.
    pub fn describe(&self) -> String {
        match (&self.eboot, self.has_archives) {
            (Eboot::Missing, _) | (_, false) => {
                "This isn't a Titanfall 2 game folder: choose the folder with the game's eboot.bin and vpk_ps4.".into()
            }
            (Eboot::Unknown, _) => "This folder's eboot.bin isn't the supported Titanfall 2 version (CUSA04013, final patch), \
                or something else has changed it."
                .into(),
            (Eboot::Original, _) if !self.runtime_installed => "Titanfall 2 found. PS4 Northstar isn't installed yet.".into(),
            _ => match &self.installed_version {
                Some(version) => format!("PS4 Northstar {version} is installed."),
                None => "PS4 Northstar is installed.".into(),
            },
        }
    }

    pub fn installable(&self) -> bool {
        self.has_archives && matches!(self.eboot, Eboot::Original | Eboot::Bootstrapped)
    }
}

pub fn inspect(folder: &Path, sources: &Sources) -> FolderInfo {
    let eboot = match fs::read(folder.join("eboot.bin")) {
        Err(_) => Eboot::Missing,
        Ok(data) => {
            let hash = sha256_hex(&data);
            if hash == sources.vanilla_eboot_sha256 {
                Eboot::Original
            } else if hash == sources.patched_eboot_sha256 {
                Eboot::Bootstrapped
            } else {
                Eboot::Unknown
            }
        }
    };
    let installed_version = fs::read_to_string(folder.join(RECORD))
        .ok()
        .and_then(|text| serde_json::from_str::<Value>(&text).ok())
        .and_then(|record| record["version"].as_str().map(str::to_string));
    let mod_count = fs::read_dir(folder.join("R2Northstar").join("mods"))
        .map(|entries| entries.flatten().filter(|e| e.path().join("mod.json").is_file()).count())
        .unwrap_or(0);
    FolderInfo {
        folder: folder.display().to_string(),
        eboot,
        has_archives: folder.join("vpk_ps4").is_dir(),
        runtime_installed: folder.join(PRX).is_file(),
        installed_version,
        mod_count,
    }
}

// ---- Releases ----

#[derive(Clone, Debug, Serialize)]
pub struct Asset {
    pub name: String,
    pub url: String,
    pub size: u64,
    pub sha256: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub struct Release {
    pub version: String,
    pub prerelease: bool,
    pub assets: Vec<Asset>,
}

impl Release {
    fn asset(&self, wanted: impl Fn(&str) -> bool) -> Option<&Asset> {
        self.assets.iter().find(|a| wanted(&a.name))
    }
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(15))
        .timeout_read(Duration::from_secs(60))
        .user_agent(&format!("NorthstarPS4TokenHelper/{}", crate::options::VERSION))
        .build()
}

/// The newest release (pre-releases included) that has a runtime and this port's mods.
pub fn latest_release(sources: &Sources) -> Result<Release, String> {
    let unreachable = || "Could not reach GitHub to find the latest release. Check the internet connection and try again.".to_string();
    let text = agent()
        .get(&sources.releases_api)
        .set("Accept", "application/vnd.github+json")
        .call()
        .map_err(|_| unreachable())?
        .into_string()
        .map_err(|_| unreachable())?;
    let releases: Value = serde_json::from_str(&text).map_err(|_| unreachable())?;
    for release in releases.as_array().into_iter().flatten() {
        if release["draft"] == true {
            continue;
        }
        let assets: Vec<Asset> = release["assets"]
            .as_array()
            .into_iter()
            .flatten()
            .map(|a| Asset {
                name: a["name"].as_str().unwrap_or_default().to_string(),
                url: a["browser_download_url"].as_str().unwrap_or_default().to_string(),
                size: a["size"].as_u64().unwrap_or(0),
                sha256: a["digest"].as_str().and_then(|d| d.strip_prefix("sha256:")).map(str::to_string),
            })
            .collect();
        let found = Release {
            version: release["tag_name"].as_str().unwrap_or_default().to_string(),
            prerelease: release["prerelease"] == true,
            assets,
        };
        if found.asset(|n| n == "northstar_ps4.prx").is_some() && found.asset(is_ps4_mods_zip).is_some() {
            return Ok(found);
        }
    }
    Err("No PS4 Northstar release with a runtime was found.".into())
}

fn is_ps4_mods_zip(name: &str) -> bool {
    name.starts_with("northstar-ps4-mods-") && name.ends_with(".zip")
}
fn is_custom_paks_zip(name: &str) -> bool {
    name.starts_with("northstar-custom-ps4-rpaks-") && name.ends_with(".zip")
}
fn is_northstar_mods_zip(name: &str) -> bool {
    name.starts_with("northstar-mods-") && name.ends_with(".zip")
}

fn megabytes(bytes: u64) -> String {
    format!("{:.1} MB", bytes as f64 / 1_048_576.0)
}

/// Downloads `url` to `path`, checking the SHA-256 when one is known.
fn download(url: &str, path: &Path, sha256: Option<&str>, label: &str, progress: &dyn Fn(String)) -> Result<(), String> {
    let failed = || format!("Downloading {label} failed. Check the internet connection and try again.");
    let response = agent().get(url).call().map_err(|_| failed())?;
    let total: Option<u64> = response.header("Content-Length").and_then(|v| v.parse().ok());
    let mut reader = response.into_reader();
    let mut file = fs::File::create(path).map_err(|e| format!("Could not save {label}: {e}"))?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0u8; 256 * 1024];
    let mut done: u64 = 0;
    let mut reported = 0u64;
    loop {
        let n = reader.read(&mut buffer).map_err(|_| failed())?;
        if n == 0 {
            break;
        }
        hasher.update(&buffer[..n]);
        file.write_all(&buffer[..n]).map_err(|e| format!("Could not save {label}: {e}"))?;
        done += n as u64;
        if done - reported >= 4 * 1_048_576 {
            reported = done;
            progress(match total {
                Some(total) if total > 0 => format!("Downloading {label}: {} of {}", megabytes(done), megabytes(total)),
                _ => format!("Downloading {label}: {}", megabytes(done)),
            });
        }
    }
    if let Some(expected) = sha256 {
        if hex::encode(hasher.finalize()) != expected.to_ascii_lowercase() {
            return Err(format!("{label} did not download correctly (its checksum doesn't match). Try again."));
        }
    }
    Ok(())
}

// ---- Zips ----

/// A zip entry's path below `prefix`, if it is one and stays inside it.
fn entry_below(name: &str, prefix: &str) -> Option<PathBuf> {
    let rest = name.replace('\\', "/");
    let rest = rest.strip_prefix(prefix)?;
    let path = PathBuf::from(rest);
    if rest.is_empty() || path.components().any(|c| !matches!(c, Component::Normal(_))) {
        return None;
    }
    Some(path)
}

/// Extracts the entries of `zip` below `prefix` into `target`. Returns how many files.
pub fn extract(zip: &Path, prefix: &str, target: &Path) -> Result<usize, String> {
    let file = fs::File::open(zip).map_err(|e| e.to_string())?;
    let mut archive = zip::ZipArchive::new(file).map_err(|e| format!("{} is not a valid zip: {e}", zip.display()))?;
    let mut count = 0;
    for i in 0..archive.len() {
        let mut entry = archive.by_index(i).map_err(|e| e.to_string())?;
        let Some(relative) = entry_below(entry.name(), prefix) else { continue };
        let out = target.join(relative);
        if entry.is_dir() {
            fs::create_dir_all(&out).map_err(|e| e.to_string())?;
            continue;
        }
        if let Some(parent) = out.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let mut data = Vec::new();
        entry.read_to_end(&mut data).map_err(|e| e.to_string())?;
        fs::write(&out, data).map_err(|e| format!("Could not write {}: {e}", out.display()))?;
        count += 1;
    }
    Ok(count)
}

/// Names of the top-level folders below `prefix` in `zip`.
fn folders_in(zip: &Path, prefix: &str) -> Result<Vec<String>, String> {
    let file = fs::File::open(zip).map_err(|e| e.to_string())?;
    let mut archive = zip::ZipArchive::new(file).map_err(|e| e.to_string())?;
    let mut names: Vec<String> = Vec::new();
    for i in 0..archive.len() {
        let entry = archive.by_index(i).map_err(|e| e.to_string())?;
        if let Some(path) = entry_below(entry.name(), prefix) {
            if let Some(Component::Normal(first)) = path.components().next() {
                let first = first.to_string_lossy().to_string();
                if path.components().count() > 1 && !names.contains(&first) {
                    names.push(first);
                }
            }
        }
    }
    Ok(names)
}

fn write_atomically(path: &Path, data: &[u8]) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let mut temp = path.as_os_str().to_owned();
    temp.push(".tmp");
    fs::write(&temp, data).map_err(|e| format!("Could not write {}: {e}", path.display()))?;
    fs::rename(&temp, path).map_err(|e| format!("Could not replace {}: {e}", path.display()))
}

// ---- Install, update, uninstall ----

const MODS_PREFIX: &str = "R2Northstar/mods/";

/// Installs (or updates to) the newest release. Returns its version.
pub fn install(folder: &Path, sources: &Sources, progress: &dyn Fn(String)) -> Result<String, String> {
    let info = inspect(folder, sources);
    if !info.installable() {
        return Err(info.describe());
    }
    progress("Looking for the latest PS4 Northstar release...".into());
    let release = latest_release(sources)?;
    progress(format!("Latest release: {}.", release.version));

    let work = std::env::temp_dir().join(format!("ps4-northstar-install-{}", std::process::id()));
    let _ = fs::remove_dir_all(&work);
    fs::create_dir_all(&work).map_err(|e| e.to_string())?;
    let result = install_from(folder, &release, sources, &info, &work, progress);
    let _ = fs::remove_dir_all(&work);
    result.map(|()| release.version)
}

fn install_from(
    folder: &Path,
    release: &Release,
    sources: &Sources,
    info: &FolderInfo,
    work: &Path,
    progress: &dyn Fn(String),
) -> Result<(), String> {
    let get = |asset: &Asset| -> Result<PathBuf, String> {
        let path = work.join(&asset.name);
        progress(format!("Downloading {} ({})...", asset.name, megabytes(asset.size)));
        download(&asset.url, &path, asset.sha256.as_deref(), &asset.name, progress)?;
        Ok(path)
    };
    let prx_asset = release.asset(|n| n == "northstar_ps4.prx").ok_or("The release has no runtime.")?;
    let prx = get(prx_asset)?;
    let ps4_mods = get(release.asset(is_ps4_mods_zip).ok_or("The release has no PS4 mods.")?)?;
    let custom_paks = match release.asset(is_custom_paks_zip) {
        Some(asset) => Some(get(asset)?),
        None => None,
    };
    let northstar_mods = match release.asset(is_northstar_mods_zip) {
        Some(asset) => get(asset)?,
        None => {
            let path = work.join("northstar.zip");
            progress("Downloading Northstar's mods (Northstar 1.31.13, 102 MB)...".into());
            download(&sources.northstar_zip_url, &path, Some(&sources.northstar_zip_sha256), "Northstar 1.31.13", progress)?;
            path
        }
    };

    let mods = folder.join("R2Northstar").join("mods");
    progress("Installing Northstar's mods...".into());
    let northstar_folders = folders_in(&northstar_mods, MODS_PREFIX)?;
    if !NORTHSTAR_MODS.iter().all(|m| northstar_folders.iter().any(|f| f == m)) {
        return Err("The Northstar download doesn't contain Northstar's mods.".into());
    }
    for name in &northstar_folders {
        let _ = fs::remove_dir_all(mods.join(name));
    }
    let n = extract(&northstar_mods, MODS_PREFIX, &mods)?;
    progress(format!("Installed {} ({n} files).", northstar_folders.join(", ")));

    progress("Installing PS4 Northstar's mods...".into());
    for name in PS4_MODS {
        let _ = fs::remove_dir_all(mods.join(name));
    }
    let n = extract(&ps4_mods, MODS_PREFIX, &mods)?;
    progress(format!("Installed {} ({n} files).", folders_in(&ps4_mods, MODS_PREFIX)?.join(", ")));
    if let Some(paks) = custom_paks {
        let n = extract(&paks, MODS_PREFIX, &mods)?;
        progress(format!("Installed Northstar.Custom's PS4 textures ({n} files)."));
    }

    progress("Installing the runtime...".into());
    let runtime = fs::read(&prx).map_err(|e| e.to_string())?;
    write_atomically(&folder.join(PRX), &runtime)?;

    if info.eboot == Eboot::Original {
        progress("Adding the bootstrap to eboot.bin (the original is kept as eboot.bin.northstar-stage2.bak)...".into());
        let original = fs::read(folder.join("eboot.bin")).map_err(|e| e.to_string())?;
        let patched = patch_eboot(&original)?;
        if sha256_hex(&patched) != sources.patched_eboot_sha256 {
            return Err("The bootstrap didn't come out as expected, so eboot.bin was left unchanged.".into());
        }
        let backup = folder.join(BACKUP_NAME);
        if !backup.exists() {
            fs::write(&backup, &original).map_err(|e| format!("Could not back up eboot.bin: {e}"))?;
        }
        write_atomically(&folder.join("eboot.bin"), &patched)?;
    } else {
        progress("eboot.bin already has the bootstrap.".into());
    }

    let record = json!({
        "version": release.version,
        "installedAt": chrono::Local::now().to_rfc3339(),
        "runtimeSha256": sha256_hex(&runtime),
        "by": format!("NorthstarPS4 Token Helper {}", crate::options::VERSION),
    });
    write_atomically(&folder.join(RECORD), serde_json::to_string_pretty(&record).unwrap().as_bytes())?;
    progress(format!("PS4 Northstar {} is installed.", release.version));
    Ok(())
}

/// Takes PS4 Northstar out of the game folder: the original eboot.bin back,
/// the runtime removed, and with `remove_mods` the whole R2Northstar folder.
pub fn uninstall(folder: &Path, remove_mods: bool, sources: &Sources, progress: &dyn Fn(String)) -> Result<(), String> {
    let info = inspect(folder, sources);
    match info.eboot {
        Eboot::Bootstrapped => {
            let backup = folder.join(BACKUP_NAME);
            let original = match fs::read(&backup) {
                Ok(data) if sha256_hex(&data) == sources.vanilla_eboot_sha256 => data,
                _ => unpatch_eboot(&fs::read(folder.join("eboot.bin")).map_err(|e| e.to_string())?)?,
            };
            if sha256_hex(&original) != sources.vanilla_eboot_sha256 {
                return Err("The original eboot.bin couldn't be recovered, so nothing was changed.".into());
            }
            write_atomically(&folder.join("eboot.bin"), &original)?;
            let _ = fs::remove_file(&backup);
            progress("Restored the original eboot.bin.".into());
        }
        Eboot::Original => progress("eboot.bin is already the original.".into()),
        Eboot::Missing => return Err(info.describe()),
        Eboot::Unknown => {
            return Err("This folder's eboot.bin wasn't changed by PS4 Northstar, so the helper won't touch it. \
                Restore it from your own backup."
                .into())
        }
    }
    if folder.join(PRX).exists() {
        fs::remove_file(folder.join(PRX)).map_err(|e| format!("Could not remove the runtime: {e}"))?;
        progress("Removed the runtime.".into());
    }
    let _ = fs::remove_file(folder.join(RECORD));
    if remove_mods && folder.join("R2Northstar").exists() {
        fs::remove_dir_all(folder.join("R2Northstar")).map_err(|e| format!("Could not remove R2Northstar: {e}"))?;
        progress("Removed the R2Northstar folder and its mods.".into());
    }
    progress("PS4 Northstar is uninstalled. Your settings and sign-in in shadPS4's data folder were kept.".into());
    Ok(())
}

#[cfg(test)]
pub mod tests {
    use super::*;

    /// An eboot shaped like the real one where the bootstrap looks: an ELF
    /// header pointing at one executable PT_LOAD at 0x4000 of size 0x29dc,
    /// the original call at 0x528d and an empty gap after the segment.
    pub fn synthetic_eboot() -> Vec<u8> {
        let mut data = vec![0u8; 0x8000];
        data[0..4].copy_from_slice(b"\x7fELF");
        data[0x20..0x28].copy_from_slice(&0x40u64.to_le_bytes()); // phoff
        data[0x36..0x38].copy_from_slice(&0x38u16.to_le_bytes()); // phentsize
        data[0x38..0x3a].copy_from_slice(&2u16.to_le_bytes()); // phnum
        let ph = 0x40 + 0x38; // the second header is the executable one
        data[ph..ph + 4].copy_from_slice(&1u32.to_le_bytes());
        data[ph + 4..ph + 8].copy_from_slice(&5u32.to_le_bytes());
        data[ph + 8..ph + 16].copy_from_slice(&0x4000u64.to_le_bytes());
        data[ph + 0x20..ph + 0x28].copy_from_slice(&RX_SEGMENT_SIZE.to_le_bytes());
        data[ph + 0x28..ph + 0x30].copy_from_slice(&RX_SEGMENT_SIZE.to_le_bytes());
        data[CALL_SITE..CALL_SITE + 5].copy_from_slice(&ORIGINAL_CALL);
        data
    }

    #[test]
    fn bootstrap_round_trips() {
        let original = synthetic_eboot();
        let patched = patch_eboot(&original).unwrap();
        assert_ne!(patched, original);
        assert_eq!(&patched[0x4000 + 0x29e0 + 38..0x4000 + 0x29e0 + 38 + 5], b"/app0");
        assert_eq!(patched[CALL_SITE], 0xe8);
        assert_eq!(unpatch_eboot(&patched).unwrap(), original);
        assert!(patch_eboot(&patched).is_err(), "patching twice must fail");
        let mut changed = original.clone();
        changed[CALL_SITE + 1] ^= 1;
        assert!(patch_eboot(&changed).is_err());
        assert!(unpatch_eboot(&original).is_err());
    }

    /// The real game's eboot, when NS_TEST_EBOOT points at an original copy:
    /// the patch must give exactly the bytes Enable-Stage2Bootstrap.ps1 writes.
    #[test]
    fn real_eboot_matches_the_script() {
        let Ok(path) = std::env::var("NS_TEST_EBOOT") else { return };
        let original = fs::read(path).unwrap();
        assert_eq!(sha256_hex(&original), VANILLA_EBOOT_SHA256);
        let patched = patch_eboot(&original).unwrap();
        assert_eq!(sha256_hex(&patched), PATCHED_EBOOT_SHA256);
        assert_eq!(sha256_hex(&unpatch_eboot(&patched).unwrap()), VANILLA_EBOOT_SHA256);
    }

    #[test]
    fn zip_entries_stay_inside() {
        assert_eq!(entry_below("R2Northstar/mods/A/mod.json", MODS_PREFIX), Some(PathBuf::from("A/mod.json")));
        assert_eq!(entry_below("R2Northstar\\mods\\A\\x.txt", MODS_PREFIX), Some(PathBuf::from("A/x.txt")));
        assert_eq!(entry_below("R2Northstar/mods/../../evil", MODS_PREFIX), None);
        assert_eq!(entry_below("R2Northstar/mods//abs", MODS_PREFIX), None);
        assert_eq!(entry_below("NorthstarLauncher.exe", MODS_PREFIX), None);
        assert_eq!(entry_below("R2Northstar/mods/", MODS_PREFIX), None);
    }
}
