# DBMono

## Setup

After cloning, run the setup script from the repo root:

```bash
cd DBMono
./setup.sh
```

This will:
- Link KiCad templates to your local KiCad installation
- Set the `DBMONO_LIBS` environment variable in every installed KiCad version's
  config so that project library tables and 3D model paths using `${DBMONO_LIBS}`
  resolve to `<repo>/PCB_Hardware/libs`
- Link the VS Code C block-comment extension
