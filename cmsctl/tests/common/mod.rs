//! The stand-in control socket the contract tests speak to.
//!
//! No test here needs a database or the api: the promise is the bytes on the wire and the exit
//! status, so a listener that records one request and answers one line proves it.

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
pub fn invoke(name: &str, arguments: &[&str], reply: &str) -> (String, Output) {
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
