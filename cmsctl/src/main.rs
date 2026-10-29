//! Administrative client for the cms-learn control socket.
//!
//! This binary carries no database driver on purpose. Administrative work exists only as a request
//! to the core over the control socket, so authority, validation and audit are implemented once, in
//! the core, and cannot be reached around here.

mod cli;
mod transport;

use std::process::ExitCode;

use clap::Parser;

use crate::cli::{Cli, Command, MigrateCommand, SettingsCommand};
use crate::transport::Request;

fn request_for(command: &Command) -> Request<'_> {
    match command {
        Command::Status => Request::Status,
        Command::Settings(SettingsCommand::Plan { key, value }) => {
            Request::SettingsPlan { key, value }
        }
        Command::Settings(SettingsCommand::Apply { plan_id }) => Request::SettingsApply { plan_id },
        Command::Migrate(MigrateCommand::Plan) => Request::MigratePlan,
        Command::Migrate(MigrateCommand::Apply { plan_id }) => Request::MigrateApply { plan_id },
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let request = request_for(&cli.command);

    match transport::exchange(&cli.socket, &request) {
        Ok(response) => {
            println!("{}", response.trim_end());
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("cmsctl: {error}");
            ExitCode::FAILURE
        }
    }
}
