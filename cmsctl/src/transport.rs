//! One request and one response over the core control socket.
//!
//! Framing is a single newline-terminated JSON document in each direction. Nothing here interprets
//! an action: the server owns authority, validation and audit, and this module only carries bytes.
//! The response is read through a byte cap because the peer is trusted only as far as the socket
//! permissions go, and a bound is cheaper to verify than a promise.

use std::error::Error;
use std::fmt;
use std::io::{BufRead, BufReader, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};

/// Largest response the client will accept, so a broken or hostile peer cannot exhaust memory here.
const MAX_RESPONSE_BYTES: u64 = 1 << 20;

#[derive(serde::Serialize)]
#[serde(tag = "action", rename_all = "snake_case")]
pub enum Request<'a> {
    Status,
    SettingsPlan { key: &'a str, value: &'a str },
    SettingsApply { plan_id: &'a str },
    MigratePlan,
    MigrateApply { plan_id: &'a str },
}

#[derive(Debug)]
pub enum TransportError {
    Connect {
        path: PathBuf,
        source: std::io::Error,
    },
    Encode(serde_json::Error),
    Write(std::io::Error),
    Read(std::io::Error),
    Truncated,
}

impl fmt::Display for TransportError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Connect { path, source } => {
                write!(formatter, "cannot reach {}: {source}", path.display())
            }
            Self::Encode(source) => write!(formatter, "cannot encode the request: {source}"),
            Self::Write(source) => write!(formatter, "cannot send the request: {source}"),
            Self::Read(source) => write!(formatter, "cannot read the response: {source}"),
            Self::Truncated => write!(formatter, "the response was not terminated"),
        }
    }
}

impl Error for TransportError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Connect { source, .. } | Self::Write(source) | Self::Read(source) => Some(source),
            Self::Encode(source) => Some(source),
            Self::Truncated => None,
        }
    }
}

pub fn exchange(socket: &Path, request: &Request<'_>) -> Result<String, TransportError> {
    let mut stream = UnixStream::connect(socket).map_err(|source| TransportError::Connect {
        path: socket.to_path_buf(),
        source,
    })?;

    let mut body = serde_json::to_vec(request).map_err(TransportError::Encode)?;
    body.push(b'\n');
    stream.write_all(&body).map_err(TransportError::Write)?;

    let mut response = String::new();
    BufReader::new(stream.take(MAX_RESPONSE_BYTES))
        .read_line(&mut response)
        .map_err(TransportError::Read)?;

    if !response.ends_with('\n') {
        return Err(TransportError::Truncated);
    }
    Ok(response)
}
