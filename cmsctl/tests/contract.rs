//! What the core is promised: the exact request each verb sends, and how a refusal is reported.
//!
//! The listener here is a stand-in for the control socket. It reads the line the client wrote and
//! answers with a canned response, so the contract under test is the bytes on the wire and the exit
//! status — not a database, not the api.

mod common;

use std::path::PathBuf;
use std::process::Command;

use crate::common::invoke;

fn socket_path(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("cmsctl-{}-{name}.sock", std::process::id()))
}

#[test]
fn status_sends_the_action_the_core_answers() {
    let (request, output) = invoke("status", &["status"], r#"{"ok":true,"settings_version":7}"#);
    assert_eq!(request, "{\"action\":\"status\"}\n");
    assert!(output.status.success());
    assert!(String::from_utf8_lossy(&output.stdout).contains("settings_version"));
}

#[test]
fn ping_sends_the_liveness_action() {
    let (request, output) = invoke("ping", &["ping"], r#"{"ok":true,"settings_version":7}"#);
    assert_eq!(request, "{\"action\":\"ping\"}\n");
    assert!(output.status.success());
}

#[test]
fn elevate_names_the_beneficiary_the_capability_and_the_reason() {
    let (request, output) = invoke(
        "elevate",
        &[
            "elevate",
            "--principal",
            "77777777-8888-4999-8aaa-bbbbbbbbbbbb",
            "--capability",
            "settings:write",
            "--reason",
            "rotate the mail credential",
            "--minutes",
            "5",
        ],
        r#"{"ok":true,"elevation_id":"e","minutes":5,"live":false}"#,
    );
    assert_eq!(
        request,
        "{\"action\":\"elevation_grant\",\"principal_id\":\"77777777-8888-4999-8aaa-bbbbbbbbbbbb\",\
         \"capability\":\"settings:write\",\"reason\":\"rotate the mail credential\",\"minutes\":5}\n"
    );
    assert!(output.status.success());
}

#[test]
fn elevate_leaves_the_policy_default_alone_when_no_duration_is_given() {
    let (request, _output) = invoke(
        "elevate-default",
        &[
            "elevate",
            "--principal",
            "77777777-8888-4999-8aaa-bbbbbbbbbbbb",
            "--capability",
            "settings:write",
            "--reason",
            "rotate the mail credential",
        ],
        r#"{"ok":true,"elevation_id":"e","minutes":10,"live":false}"#,
    );
    assert!(!request.contains("minutes"));
}

#[test]
fn approve_names_only_the_elevation() {
    let (request, output) = invoke(
        "approve",
        &["approve", "aaaaaaa1-bbbb-4ccc-8ddd-eeeeeeeeeee1"],
        r#"{"ok":true,"live":true}"#,
    );
    assert_eq!(
        request,
        "{\"action\":\"elevation_approve\",\"elevation_id\":\"aaaaaaa1-bbbb-4ccc-8ddd-eeeeeeeeeee1\"}\n"
    );
    assert!(output.status.success());
}

#[test]
fn a_refusal_fails_the_invocation_and_reaches_stderr() {
    let (_request, output) = invoke(
        "refusal",
        &["status"],
        r#"{"ok":false,"error":"the account that granted an elevation cannot approve it"}"#,
    );
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("cannot approve"));
    assert!(String::from_utf8_lossy(&output.stdout).is_empty());
}

#[test]
fn a_client_with_no_core_to_talk_to_says_so() {
    let output = Command::new(env!("CARGO_BIN_EXE_cmsctl"))
        .arg("--socket")
        .arg(socket_path("absent"))
        .arg("status")
        .output()
        .expect("run cmsctl");
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("cannot reach"));
}

#[test]
fn a_plan_prints_the_diff_the_core_would_make() {
    let plan_id = "0b6f4d1e-0000-4000-8000-000000000001";
    let reply = format!(
        r#"{{"ok":true,"plan_id":"{plan_id}","key":"mail.host","tier":"operator","reload":"hot","old_value":"mail.example.org","new_value":"mail.example.net","expires_at":"2026-11-01T00:00:00Z"}}"#
    );
    let (_, output) = invoke(
        "plan",
        &[
            "settings",
            "plan",
            "--key",
            "mail.host",
            "--value",
            "mail.example.net",
        ],
        &reply,
    );
    assert!(output.status.success());
    let printed = String::from_utf8_lossy(&output.stdout);
    assert!(printed.contains(&format!("plan {plan_id}")));
    assert!(printed.contains("mail.host (operator, applies without a restart)"));
    assert!(printed.contains("mail.example.org -> mail.example.net"));
    assert!(printed.contains(&format!("apply with: cmsctl settings apply {plan_id}")));
}

#[test]
fn a_plan_answer_missing_part_of_the_diff_fails_the_invocation() {
    // The operator reads the change before it lands, so a reply carrying only part of it is refused rather
    // than printed as though it were the whole change.
    let (_, output) = invoke(
        "partial-plan",
        &["settings", "plan", "--key", "mail.host", "--value", "x"],
        r#"{"ok":true,"plan_id":"0b6f4d1e-0000-4000-8000-000000000001"}"#,
    );
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("the answer carries no reload"));
}
