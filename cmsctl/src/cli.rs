//! The command surface.
//!
//! Every verb here maps to one action the core performs, and nothing more: an option that the core
//! cannot honour would be a promise this binary cannot keep. A settings change is two-phase, so both
//! halves are offered: `plan` records the change and prints the diff it would make, `apply` lands
//! exactly the plan it is given, once. `apply` takes no value, because the value was decided when the
//! diff was read.

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
    /// Report the settings version the core is serving.
    Status,

    /// Ask for a capability to be granted to an account for a while.
    Elevate {
        /// Account that will hold the capability.
        #[arg(long, value_name = "UUID")]
        principal: String,

        /// Capability name, for example `settings:write`.
        #[arg(long, value_name = "NAME")]
        capability: String,

        /// Account asking for it. Never the account receiving it.
        #[arg(long, value_name = "UUID")]
        granted_by: String,

        /// Why the elevation exists. An approver is asked to judge this, so it is required.
        #[arg(long, value_name = "TEXT")]
        reason: String,

        /// Minutes it should last. Omit for the policy default; the core caps it at 60.
        #[arg(long, value_name = "MINUTES")]
        minutes: Option<i64>,
    },

    /// Make a pending elevation live as the second person.
    Approve {
        /// Identifier returned by `cmsctl elevate`.
        elevation_id: String,

        /// Account approving. Must be neither the beneficiary nor the account that asked.
        #[arg(long, value_name = "UUID")]
        approved_by: String,
    },

    /// Change a setting, or land a change that was planned.
    Settings {
        #[command(subcommand)]
        action: SettingsAction,
    },
}

#[derive(Debug, Subcommand)]
pub enum SettingsAction {
    /// Record a change and print the diff it would make, without storing it.
    Plan {
        /// Setting key, as the catalogue declares it.
        #[arg(long, value_name = "KEY")]
        key: String,

        /// Value the setting would take.
        #[arg(long, value_name = "VALUE")]
        value: String,

        /// Account the plan is attributed to.
        #[arg(long, value_name = "UUID")]
        author: String,
    },

    /// Land exactly one planned change, once.
    Apply {
        /// Identifier returned by `cmsctl settings plan`.
        plan_id: String,

        /// Account the change is attributed to.
        #[arg(long, value_name = "UUID")]
        actor: String,
    },
}
