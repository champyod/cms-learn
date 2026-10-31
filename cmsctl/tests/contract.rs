//! What the core is promised: the exact request each verb sends, and how a refusal is reported.
//!
//! The listener here is a stand-in for the control socket. It reads the line the client wrote and
//! answers with a canned response, so the contract under test is the bytes on the wire and the exit
//! status — not a database, not the api.

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixListener;
use std::path::PathBuf;
use std::process::{Command, Output};

fn socket_path(name: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!("cmsctl-{}-{name}.sock", std::process::id()));
    let _ = std::fs::remove_file(&path);
    path
}

/// Runs one invocation against a listener that records the request and answers `reply`.
fn invoke(name: &str, arguments: &[&str], reply: &str) -> (String, Output) {
    let path = socket_path(name);
    let listener = UnixListener::bind(&path).expect("bind the stand-in socket");

    let socket = path.to_str().expect("the socket path is UTF-8").to_owned();
    let args: Vec<String> = arguments.iter().map(|value| (*value).to_owned()).collect();
    let child = std::thread::spawn(move || {
        Command::new(env!("CARGO_BIN_EXE_cmsctl"))
            .arg("--socket")
            .arg(&socket)
            .args(args)
            .output()
            .expect("run cmsctl")
    });

    let (stream, _) = listener.accept().expect("the client connected");
    let mut request = String::new();
    BufReader::new(&stream)
        .read_line(&mut request)
        .expect("read the request line");
    writeln!(&stream, "{reply}").expect("answer the request");

    let output = child.join().expect("the client finished");
    let _ = std::fs::remove_file(&path);
    (request, output)
}

#[test]
fn status_sends_the_action_the_core_answers() {
    let (request, output) = invoke("status", &["status"], r#"{"ok":true,"settings_version":7}"#);
    assert_eq!(request, "{\"action\":\"status\"}\n");
    assert!(output.status.success());
    assert!(String::from_utf8_lossy(&output.stdout).contains("settings_version"));
}

#[test]
fn elevate_names_the_beneficiary_the_grantor_and_the_reason() {
    let (request, output) = invoke(
        "elevate",
        &[
            "elevate",
            "--principal",
            "77777777-8888-4999-8aaa-bbbbbbbbbbbb",
            "--capability",
            "settings:write",
            "--granted-by",
            "88888888-9999-4aaa-8bbb-cccccccccccc",
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
         \"capability\":\"settings:write\",\"granted_by\":\"88888888-9999-4aaa-8bbb-cccccccccccc\",\
         \"reason\":\"rotate the mail credential\",\"minutes\":5}\n"
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
            "--granted-by",
            "88888888-9999-4aaa-8bbb-cccccccccccc",
            "--reason",
            "rotate the mail credential",
        ],
        r#"{"ok":true,"elevation_id":"e","minutes":10,"live":false}"#,
    );
    assert!(!request.contains("minutes"));
}

#[test]
fn approve_names_the_second_person() {
    let (request, output) = invoke(
        "approve",
        &[
            "approve",
            "aaaaaaa1-bbbb-4ccc-8ddd-eeeeeeeeeee1",
            "--approved-by",
            "99999999-aaaa-4bbb-8ccc-dddddddddddd",
        ],
        r#"{"ok":true,"live":true}"#,
    );
    assert_eq!(
        request,
        "{\"action\":\"elevation_approve\",\"elevation_id\":\"aaaaaaa1-bbbb-4ccc-8ddd-eeeeeeeeeee1\",\
         \"approved_by\":\"99999999-aaaa-4bbb-8ccc-dddddddddddd\"}\n"
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
