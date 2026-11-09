# pf-focus-client fixtures

Byte-identical copies of the producer fixtures in `pocketforge-os/runtime` at
commit `7b97af8ffe1acb2665b946f97f4473728ba2ccb1` (merge of runtime#199):

| file | runtime path | sha256 |
| --- | --- | --- |
| `register.json` | `crates/pf-input-ipc/tests/golden/register.json` | `0569e0bbb63bee955a3057bac80b0e9725b4655b3626ba90361fef76f8934cf3` |
| `state_snapshot.json` | `crates/pf-input-ipc/tests/golden/state_snapshot.json` | `8f3b73c0d966c93a952774e188b3635158557a5ff52b1281c87047498868a1b8` |
| `focus.json` | `crates/pf-input-ipc/tests/golden/focus.json` | `5a6ae441c8a463e99bc2cbd8b6121a835dbae04179e43339953b97e78b02baed` |
| `app_id_from_unit.tsv` | `crates/pf-peer-identity/tests/fixtures/app_id_from_unit.tsv` | `a436bcce2fed35695afe82029221163547252d81040d66c976ace48a164e9bfa` |

Do not edit them here. Regenerate them in runtime (`PF_UPDATE_GOLDEN=1`) and
copy them again. The JSON files have no trailing newline.
