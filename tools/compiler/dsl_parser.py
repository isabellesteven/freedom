import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Union


@dataclass
class Endpoint:
    name: str
    pin: Optional[str] = None


@dataclass
class IoDecl:
    direction: str  # input|output
    name: str
    sample_fmt: str  # f32|s16
    sample_rate_khz: int
    block: int
    channels: int


@dataclass
class NodeDecl:
    name: str
    module: str
    params: Dict[str, Union[float, int, str]] = field(default_factory=dict)


@dataclass
class ConnectStmt:
    src: Endpoint
    dst: Endpoint


@dataclass
class Graph:
    name: str
    ios: List[IoDecl]
    nodes: List[NodeDecl]
    connects: List[ConnectStmt]


_GRAPH_RE = re.compile(r'^graph\s+"([^"]+)"\s*$')
_END_RE = re.compile(r"^end\s*$")
_IO_RE = re.compile(
    r"^io\s+(input|output)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*"
    r"(f32|s16)@(-?\d+)k\s+block=(-?\d+)\s+ch=(-?\d+)\s*$"
)
_NODE_RE = re.compile(
    r"^node\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*\((.*)\)\s*)?$"
)
_CONN_RE = re.compile(r"^connect\s+(.+?)\s*->\s*(.+?)\s*$")
_ENDPOINT_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?:\.([A-Za-z_][A-Za-z0-9_]*))?$")
_NUM_RE = re.compile(r"^-?\d+(?:\.\d+)?$")


def _strip_comments(line: str) -> str:
    in_str = False
    out = []
    for ch in line:
        if ch == '"':
            in_str = not in_str
            out.append(ch)
            continue
        if ch == '#' and not in_str:
            break
        out.append(ch)
    return "".join(out).strip()


def _parse_endpoint(text: str) -> Endpoint:
    m = _ENDPOINT_RE.match(text.strip())
    if not m:
        raise ValueError(f"invalid endpoint: {text!r}")
    return Endpoint(name=m.group(1), pin=m.group(2))


def _split_params(text: str) -> List[str]:
    parts: List[str] = []
    cur = []
    in_str = False
    for ch in text:
        if ch == '"':
            in_str = not in_str
            cur.append(ch)
            continue
        if ch == ',' and not in_str:
            part = "".join(cur).strip()
            if part:
                parts.append(part)
            cur = []
            continue
        cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def _parse_value(raw: str) -> Union[float, int, str]:
    raw = raw.strip()
    if raw.startswith('"') and raw.endswith('"') and len(raw) >= 2:
        return raw[1:-1]
    if not _NUM_RE.match(raw):
        raise ValueError(f"invalid value: {raw!r}")
    if "." in raw:
        return float(raw)
    return int(raw)


def parse_dsl(text: str) -> Graph:
    graph_name: Optional[str] = None
    ios: List[IoDecl] = []
    nodes: List[NodeDecl] = []
    connects: List[ConnectStmt] = []

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = _strip_comments(raw)
        if not line:
            continue

        m = _GRAPH_RE.match(line)
        if m:
            if graph_name is not None:
                raise ValueError(f"line {lineno}: duplicate graph declaration")
            graph_name = m.group(1)
            continue

        if _END_RE.match(line):
            break

        m = _IO_RE.match(line)
        if m:
            io = IoDecl(
                direction=m.group(1),
                name=m.group(2),
                sample_fmt=m.group(3),
                sample_rate_khz=int(m.group(4)),
                block=int(m.group(5)),
                channels=int(m.group(6)),
            )
            if io.sample_rate_khz <= 0 or io.block <= 0 or io.channels <= 0:
                raise ValueError(f"line {lineno}: io fields must be positive")
            ios.append(io)
            continue

        m = _NODE_RE.match(line)
        if m:
            name = m.group(1)
            module = m.group(2)
            params_raw = (m.group(3) or "").strip()
            params: Dict[str, Union[float, int, str]] = {}
            if params_raw:
                for part in _split_params(params_raw):
                    if "=" not in part:
                        raise ValueError(f"line {lineno}: invalid param {part!r}")
                    key, value = part.split("=", 1)
                    key = key.strip()
                    if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", key):
                        raise ValueError(f"line {lineno}: invalid param key {key!r}")
                    params[key] = _parse_value(value)
            nodes.append(NodeDecl(name=name, module=module, params=params))
            continue

        m = _CONN_RE.match(line)
        if m:
            connects.append(ConnectStmt(src=_parse_endpoint(m.group(1)), dst=_parse_endpoint(m.group(2))))
            continue

        raise ValueError(f"line {lineno}: unsupported statement: {line!r}")

    if graph_name is None:
        raise ValueError("missing graph declaration")
    if not ios:
        raise ValueError("graph must declare at least one io")
    if not nodes:
        raise ValueError("graph must declare at least one node")

    return Graph(name=graph_name, ios=ios, nodes=nodes, connects=connects)