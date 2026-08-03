"""
Driver: load -> build IR -> render a backend's templates.

The render loop is generic. A backend supplies a template, an output path, and
a marker -> handler map; it never sees the loop.
"""

import json
import yaml

import reg_ir
import backend_c


MAP_FILE    = "global_reg.yaml"
SCHEMA_FILE = "register.json"


def render(template_path, output_path, markers, ir):
    with open(template_path) as tmpl, open(output_path, "w") as out:
        for line in tmpl:
            handler = markers.get(line.strip())
            out.write(handler(ir) if handler else line)


def main():
    source = yaml.safe_load(open(MAP_FILE))
    schema = json.load(open(SCHEMA_FILE))

    ir = reg_ir.build_ir(source, schema)

    render(backend_c.TEMPLATE, backend_c.OUTPUT, backend_c.MARKERS, ir)


if __name__ == "__main__":
    main()
