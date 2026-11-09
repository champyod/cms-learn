//! The command surface.
//!
//! Every verb here maps to one action the core performs, and nothing more: an option that the core
//! cannot honour would be a promise this binary cannot keep. The socket is the identity, so no verb
//! takes an actor: a command that asked for one would let a user believe they act as themselves.
//! A settings change is two-phase, so both halves are offered: `plan` records the change and prints the
//! diff it would make, `apply` lands exactly the plan it is given, once. `apply` takes no value, because
//! the value was decided when the diff was read.

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
    ///
    /// Acts as the system account over the control socket.
    Status,

    /// Ask the core to answer.
    ///
    /// Acts as the system account over the control socket.
    Ping,

    /// Ask for a capability to be granted to an account for a while.
    ///
    /// Acts as the system account over the control socket.
    Elevate {
        /// Account that will hold the capability.
        #[arg(long, value_name = "UUID")]
        principal: String,

        /// Capability name, for example `settings:write`.
        #[arg(long, value_name = "NAME")]
        capability: String,

        /// Why the elevation exists. An approver is asked to judge it, so it is required.
        #[arg(long, value_name = "TEXT")]
        reason: String,

        /// Minutes it should last. Omit for the policy default; the core caps it at 60.
        #[arg(long, value_name = "MINUTES")]
        minutes: Option<i64>,
    },

    /// Make a pending elevation live as the second person.
    ///
    /// Acts as the system account over the control socket.
    Approve {
        /// Identifier returned by `cmsctl elevate`.
        #[arg(value_name = "ELEVATION_ID")]
        elevation_id: String,
    },

    /// Change a setting, or land a change that was planned.
    ///
    /// Acts as the system account over the control socket.
    Settings {
        #[command(subcommand)]
        action: SettingsAction,
    },

    /// Create or maintain accounts.
    ///
    /// Acts as the system account over the control socket.
    Account {
        #[command(subcommand)]
        action: AccountAction,
    },
}

#[derive(Debug, Subcommand)]
pub enum SettingsAction {
    /// Record a change and print the diff it would make, without storing it.
    ///
    /// Acts as the system account over the control socket.
    Plan {
        /// Setting key, as the catalogue declares it.
        #[arg(long, value_name = "KEY")]
        key: String,

        /// Value the setting would take.
        #[arg(long, value_name = "VALUE")]
        value: String,
    },

    /// Land exactly one planned change, once.
    ///
    /// Acts as the system account over the control socket.
    Apply {
        /// Identifier returned by `cmsctl settings plan`.
        #[arg(value_name = "PLAN_ID")]
        plan_id: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum AccountAction {
    /// Create a learner account that is active on the administrator's vouch.
    ///
    /// Acts as the system account over the control socket.
    CreateLearner {
        /// Handle the new account signs in with.
        #[arg(long, value_name = "HANDLE")]
        handle: String,

        /// Address for verification mail; required only when the site requires one.
        #[arg(long, value_name = "EMAIL")]
        email: Option<String>,

        /// Level the learner works at.
        #[arg(long, value_name = "ID")]
        level_id: Option<String>,

        /// Initial password; omit to create the account without a credential.
        #[arg(long, value_name = "TEXT")]
        password: Option<String>,
    },

    /// Create an administrator account and grant it the roles named.
    ///
    /// Acts as the system account over the control socket.
    CreateAdmin {
        /// Handle the new account signs in with.
        #[arg(long, value_name = "HANDLE")]
        handle: String,

        /// Roles to grant, comma separated; each must be one the system account holds.
        #[arg(long, value_name = "ROLES")]
        roles: Option<String>,

        /// Initial password; omit to create the account without a credential.
        #[arg(long, value_name = "TEXT")]
        password: Option<String>,
    },

    /// Approve a learner account that is waiting for an operator.
    ///
    /// Acts as the system account over the control socket.
    Approve {
        /// Learner account being approved.
        #[arg(value_name = "LEARNER_ID")]
        learner_id: String,
    },

    /// Issue a fresh verification token for a learner that has an address.
    ///
    /// Acts as the system account over the control socket.
    IssueVerification {
        /// Learner the token belongs to.
        #[arg(value_name = "LEARNER_ID")]
        learner_id: String,
    },

    /// Revoke an administrator account other than the system account.
    ///
    /// Acts as the system account over the control socket.
    RevokeAdmin {
        /// Administrator being revoked.
        #[arg(value_name = "ADMIN_ID")]
        admin_id: String,
    },
}
