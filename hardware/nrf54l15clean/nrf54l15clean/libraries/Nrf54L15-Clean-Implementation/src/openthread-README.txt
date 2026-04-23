OpenThread public headers imported for the nRF54L15 Thread PAL skeleton.

Imported from:
- repository: https://github.com/openthread/openthread
- commit: 254043deece3b8b372659dc2b79b84fa923483b8

Imported paths:
- include/openthread
- examples/platforms/openthread-system.h
- LICENSE

Refresh helper:
- scripts/import_openthread_public_headers.sh

Scope note:
- this is a public-header import for the compile-valid platform skeleton
- it is not a full OpenThread core/vendor drop yet
- the future full-core staging path is
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core`
- the repo-local scaffold script for that next step is
  `scripts/import_openthread_core_scaffold.sh`
