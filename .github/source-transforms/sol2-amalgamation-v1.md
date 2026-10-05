# sol2 amalgamation receipt v1

- Canonical upstream: `https://github.com/ThePhD/sol2.git`
- Upstream base: `2b0d2fe8ba0074e16b499940c4f3126b9c7d3471`
- PocketForge source pin: `9755a852628ca0abfa8a05ec1fa475fb2609b2e1`
- Output: `thirdparty/sol/sol.hpp`
- Output SHA-256: `81ea0b4779f70ad4a4fc2b08da302cd2317ad066682f448a611a530c681844e6`

The validator exports the exact PocketForge commit, runs its source-owned
`tests/single_header_snapshot.py`, and requires that test's deterministic
amalgamation digest to equal the manifest digest above. The source test runs
`single/single.py` twice with fixed Git metadata and normalizes only the
generated UTC timestamp to `// Generated <normalized> UTC`. The committed
header is that normalized output. No generated implementation bytes are edited
after amalgamation.
