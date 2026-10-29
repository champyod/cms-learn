//! The command surface.
//!
//! Every verb here maps to one action the core performs, and nothing more: an option that the core
//! cannot honour would be a promise this binary cannot keep.

use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Debug, Parser)]
#[command(
    name = "cmsctl",
    version,
    about = "Administer a cms-learn installation"
)]
pub struct Cli {
    /// Path to the core control socket.
    #[arg(
        long,
        global = true,
        value_name = "PATH",
        default_value = "/run/cmslearn/admin.sock"
    )]
    pub socket: PathBuf,

    #[command(subcommand)]
    pub command: Command,
}

#[derive(Debug, Subcommand)]
pub enum Command {
    /// Report the core version, schema version and settings version.
    Status,

    /// Change configuration.
    #[command(subcommand)]
    Settings(SettingsCommand),

    /// Change the database schema.
    #[command(subcommand)]
    Migrate(MigrateCommand),
}

#[derive(Debug, Subcommand)]
pub enum SettingsCommand {
    /// Show what a change would do, without doing it.
    Plan {
        /// Settings key, for example `registration_open`.
        key: String,
        /// Value as it would be entered in the admin interface.
        value: String,
    },

    /// Apply a plan that was previously generated.
    Apply {
        /// Identifier returned by `settings plan`.
        plan_id: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum MigrateCommand {
    /// Show the statements a migration would run, without running them.
    Plan,

    /// Apply a migration plan that was previously generated.
    Apply {
        /// Identifier returned by `migrate plan`.
        plan_id: String,
    },
}
