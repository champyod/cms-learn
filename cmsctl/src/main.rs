//! Administrative client for the cms-learn control socket.
//!
//! This binary carries no database driver on purpose. Administrative work exists only as a request
//! to the core over the control socket, so authority, validation and audit are implemented once, in
//! the core, and cannot be reached around here.

mod cli;
mod transport;

use std::process::ExitCode;

use clap::Parser;

use crate::cli::{Cli, Command};
use crate::transport::Request;

fn request_for(command: &Command) -> Request<'_> {
    match command {
        Command::Status => Request::Status,
        Command::Elevate {
            principal,
            capability,
            granted_by,
            reason,
            minutes,
        } => Request::ElevationGrant {
            principal_id: principal,
            capability,
            granted_by,
            reason,
            minutes: *minutes,
        },
        Command::Approve {
            elevation_id,
            approved_by,
        } => Request::ElevationApprove {
            elevation_id,
            approved_by,
        },
    }
}

/// Whether the core refused the action. An answer this client cannot read is a refusal too: a verb that
/// exited zero on an unreadable reply would tell a script the change landed.
fn is_refusal(response: &str) -> bool {
    serde_json::from_str::<serde_json::Value>(response)
        .map(|value| value.get("ok").and_then(serde_json::Value::as_bool) != Some(true))
        .unwrap_or(true)
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let request = request_for(&cli.command);

    match transport::exchange(&cli.socket, &request) {
        Ok(response) => {
            if is_refusal(&response) {
                eprintln!("cmsctl: {}", response.trim_end());
                return ExitCode::FAILURE;
            }
            println!("{}", response.trim_end());
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("cmsctl: {error}");
            ExitCode::FAILURE
        }
    }
}
