//! The exact request each account verb sends, field for field.

mod common;

use crate::common::invoke;

/// Runs one account verb, which answers with a subject and a detail line on success.
fn account_request(name: &str, arguments: &[&str], expected: &str) {
    let (request, output) = invoke(
        name,
        arguments,
        r#"{"ok":true,"subject":"s","detail":"done"}"#,
    );
    assert_eq!(request, expected);
    assert!(output.status.success());
    assert!(String::from_utf8_lossy(&output.stdout).contains("done"));
}

#[test]
fn account_create_learner_sends_every_field_it_was_given() {
    account_request(
        "create-learner",
        &[
            "account",
            "create-learner",
            "--handle",
            "alice",
            "--email",
            "alice@example.org",
            "--level-id",
            "beginner",
            "--password",
            "a long password",
        ],
        "{\"action\":\"account_create_learner\",\"handle\":\"alice\",\
         \"email\":\"alice@example.org\",\"level_id\":\"beginner\",\
         \"password\":\"a long password\"}\n",
    );
}

#[test]
fn account_create_learner_omits_the_optional_fields_it_was_not_given() {
    account_request(
        "create-learner-minimal",
        &["account", "create-learner", "--handle", "alice"],
        "{\"action\":\"account_create_learner\",\"handle\":\"alice\"}\n",
    );
}

#[test]
fn account_create_admin_sends_the_roles_as_one_comma_separated_field() {
    account_request(
        "create-admin",
        &[
            "account",
            "create-admin",
            "--handle",
            "operator",
            "--roles",
            "11111111-2222-4333-8444-555555555555,66666666-7777-4888-8999-aaaaaaaaaaaa",
            "--password",
            "a long password",
        ],
        "{\"action\":\"account_create_admin\",\"handle\":\"operator\",\
         \"roles\":\"11111111-2222-4333-8444-555555555555,66666666-7777-4888-8999-aaaaaaaaaaaa\",\
         \"password\":\"a long password\"}\n",
    );
}

#[test]
fn account_approve_names_the_learner() {
    account_request(
        "account-approve",
        &["account", "approve", "77777777-8888-4999-8aaa-bbbbbbbbbbbb"],
        "{\"action\":\"account_approve\",\"learner_id\":\"77777777-8888-4999-8aaa-bbbbbbbbbbbb\"}\n",
    );
}

#[test]
fn account_issue_verification_names_the_learner() {
    account_request(
        "account-verify",
        &[
            "account",
            "issue-verification",
            "77777777-8888-4999-8aaa-bbbbbbbbbbbb",
        ],
        "{\"action\":\"account_issue_verification\",\
         \"learner_id\":\"77777777-8888-4999-8aaa-bbbbbbbbbbbb\"}\n",
    );
}

#[test]
fn account_revoke_admin_names_the_administrator() {
    account_request(
        "account-revoke",
        &[
            "account",
            "revoke-admin",
            "88888888-9999-4aaa-8bbb-cccccccccccc",
        ],
        "{\"action\":\"account_revoke_admin\",\
         \"admin_id\":\"88888888-9999-4aaa-8bbb-cccccccccccc\"}\n",
    );
}
