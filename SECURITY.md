# Security Policy

## Security & Data Integrity Commitments

Salvus handles raw storage devices and untrusted file-system metadata. The following controls are mandatory across the codebase:

- **Read-only source access:** Storage devices and disk images are opened read-only for scanning and analysis.
- **Separate recovery destination:** Recovered files must never be written to the source volume or physical disk.
- **Strict bounds validation:** All disk offsets, sector boundaries, allocation lengths, and IPC frame sizes are validated before use.
- **No automatic execution:** Recovered files are never executed automatically, and previews use safe, non-executing renderers.
- **Restricted IPC:** The Windows Named Pipe between `Salvus.Desktop` and `Salvus.Engine` rejects remote clients (`PIPE_REJECT_REMOTE_CLIENTS`) and uses per-session pipe names restricted to the current user.
- **No telemetry:** Salvus does not collect telemetry or upload user data.

## Reporting a Vulnerability

If you discover a security vulnerability, memory-safety issue (e.g., buffer overflow in a file-system parser), privilege-escalation vector, or data-integrity bug:

1. **Do not** open a public issue with exploit details.
2. Report the issue privately to the maintainers using GitHub Security Advisories (or the designated security contact once published).
3. Include a clear description of the issue, affected component(s), reproduction steps (using disposable disk images or synthetic test inputs), and potential impact.

We will acknowledge reports promptly, coordinate a fix, and credit reporters unless anonymity is requested.
