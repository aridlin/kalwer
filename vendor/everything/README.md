# Everything integration

Official IPC header from https://www.voidtools.com/Everything-SDK.zip, downloaded 2026-09-06. Kalwer uses the supported asynchronous WM_COPYDATA protocol directly with bounded waits.

Bundled official x64 installer: https://www.voidtools.com/Everything-1.4.1.1032.x64-Setup.exe
SHA-256 c42efad041d4c0bb4d4ac97ae7cbe89f153ec1fe078772392e749c7f5d5282d3, verified against https://www.voidtools.com/Everything-1.4.1.1032.sha256.

The installer runs only through /index-setup, uses its normal UI/UAC, and installs the service in its normal protected location. Existing Everything installations are reused. License.txt contains the upstream distribution notices; SDK source is MIT licensed by David Carpenter (2022).
