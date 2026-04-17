# Security Policy

## Supported Versions

Currently, only the latest major release of NeuroPace RDNA receives security updates.

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |
| < 0.1.0 | :x:                |

## Reporting a Vulnerability

We take the security of NeuroPace RDNA very seriously. Since this software runs locally as a Ring-3 background agent, the primary threat vectors involve privilege escalation and unintended IPC (Inter-Process Communication) exploitation.

If you discover a security vulnerability, please do NOT report it by creating a public GitHub issue. 

Please disclose any vulnerabilities directly to the maintainer:
- **Email:** Report it directly to the repository maintainer's primary contact.
- **Discord:** Contact `cxnsole` privately.

### What to include
Please supply the following information with your report:
- A description of the vulnerability.
- Steps to reproduce it (a Proof of Concept).
- Potential impact on system security or stability.

We will try to acknowledge your report within 48 hours and provide a timeline for the mitigation patch and CVE resolution if applicable.
