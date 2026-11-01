//! Administrative client for the cms-learn control socket.
//!
//! This binary carries no database driver on purpose. Administrative work exists only as a request
//! to the core over the control socket, so authority, validation and audit are implemented once, in
//! the core, and cannot be reached around here.

mod cli;
mod transport;

use std::process::ExitCode;

use clap::Parser;

use crate::cli::{Cli, Command, SettingsAction};
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
        Command::Settings { action } => match action {
            SettingsAction::Plan { key, value, author } => Request::SettingsPlan {
                key,
                value,
                author_id: author,
            },
            SettingsAction::Apply { plan_id, actor } => Request::SettingsApply {
                plan_id,
                actor_id: actor,
            },
        },
    }
}

/// One string field of an answer, or the reason the answer is not the shape this verb promised.
fn text<'a>(value: &'a serde_json::Value, name: &str) -> Result<&'a str, String> {
    value
        .get(name)
        .and_then(serde_json::Value::as_str)
        .ok_or_else(|| format!("the answer carries no {name}"))
}

/// Prints the diff a plan would make. The whole point of planning is that the operator reads the change
/// before it lands, so a reply missing part of the diff fails the invocation rather than printing a
/// partial one.
fn print_plan(response: &str) -> Result<(), String> {
    let value: serde_json::Value = serde_json::from_str(response)
        .map_err(|error| format!("cannot read the answer: {error}"))?;
    let plan_id = text(&value, "plan_id")?;
    let reload = match text(&value, "reload")? {
        "hot" => "applies without a restart",
        "restart" => "applies on the next restart",
        other => return Err(format!("the answer names an unknown reload class: {other}")),
    };

    println!("plan {plan_id}");
    println!(
        "  {} ({}, {reload})",
        text(&value, "key")?,
        text(&value, "tier")?
    );
    println!(
        "  {} -> {}",
        text(&value, "old_value")?,
        text(&value, "new_value")?
    );
    println!("  expires {}", text(&value, "expires_at")?);
    println!("apply with: cmsctl settings apply {plan_id} --actor <uuid>");
    Ok(())
}

/// Whether this verb answered with a diff rather than a bare acknowledgement.
fn is_plan(command: &Command) -> bool {
    matches!(
        command,
        Command::Settings {
            action: SettingsAction::Plan { .. }
        }
    )
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
            if is_plan(&cli.command) {
                if let Err(error) = print_plan(&response) {
                    eprintln!("cmsctl: {error}");
                    return ExitCode::FAILURE;
                }
            } else {
                println!("{}", response.trim_end());
            }
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("cmsctl: {error}");
            ExitCode::FAILURE
        }
    }
}
