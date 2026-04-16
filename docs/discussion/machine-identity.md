I also think machines should have an identity (a UUID) and a descriptive name or label string, «Level 3 Fileserver», or «Print Server», «Teletext Server».

## Response:
Yes — 100% yes. This is not just reasonable, it’s _foundational_. You’re converging on exactly the model that keeps the UX humane while the system grows more powerful.

Let’s firm this up and connect it cleanly to everything you’ve already decided.

* * *

1\. Identity vs name: you need both, and they do different jobs
---------------------------------------------------------------

### Machine identity (UUID)

*   **Opaque**
*   **Stable for the lifetime of the machine**
*   **Never shown to users** (except maybe in diagnostics)
*   Used for:
    *   Reconnection
    *   Multi-client coordination
    *   Avoiding ambiguity
    *   Persisted references (templates, history, logs)

Think of this as:

> _“Who are you, really?”_

This is non-negotiable once you allow:

*   Multiple machines
*   Headless operation
*   Network discovery
*   Reconnection

* * *

### Machine name / label (user-facing)

*   **Human-readable**
*   **Editable**
*   **Non-unique**
*   **Contextual**

Examples you gave are _excellent_:

*   “Level 3 Fileserver”
*   “Print Server”
*   “Teletext Server”
*   “Master 128 – Econet”

This answers:

> _“What is this machine for?”_

And that’s exactly what users care about.

* * *

2\. Where the name comes from (important UX detail)
---------------------------------------------------

### Default naming strategy

On creation:

*   Start with a sensible default derived from:
    *   Model
    *   Preset
    *   Role (if known)

Examples:

*   “BBC Model B”
*   “Master 128 (DFS)”
*   “Model B – Econet Node”

Then:

*   Encourage renaming, but don’t force it
*   Allow renaming at any time

Apple precedent:

*   “Untitled”
*   “New Document”
*   “My Mac”

* * *

3\. Where the name lives architecturally
----------------------------------------

The **machine name belongs to the machine**, not the front end.

That means:

*   Stored by the core
*   Returned via gRPC
*   Advertised over Bonjour
*   Seen consistently by all clients

This is key for:

*   Multi-client setups
*   Remote management
*   Headless servers

A Python client, macOS UI, or future web UI should all see:

> “Teletext Server”, not invent their own label.

* * *

4\. Identity + provenance + name = a complete mental model
----------------------------------------------------------

You now have three orthogonal concepts:

| Concept | Purpose | Visible? |
| --- | --- | --- |
| UUID | Stable identity | ❌ |
| Name | Human meaning | ✅ |
| Provenance | How it was launched | ⚠️ (softly) |

This is a _very strong_ separation of concerns.

* * *

5\. How this shows up in the UI (nicely)
----------------------------------------

### Machines menu

```
Machines
────────────
Running Locally
  ▸ Teletext Server
  ▸ Level 3 Fileserver
────────────
Connected
  ▸ Print Server @ lab.local
```

No technical noise. Just names that mean something.

* * *

### Quit dialog

```
Running Locally
 ▸ Teletext Server        (Started by Beebium)
 ▸ Level 3 Fileserver    (Started from Terminal)
```

Provenance is _supporting detail_, not the headline.

* * *

### Bonjour discovery

Advertise:

*   Service name: `Teletext Server`
*   Service type: `_beebium._tcp`
*   TXT records:
    *   `uuid=…`
    *   `model=master128`
    *   `role=fileserver` (optional)

This makes discovery robust and debuggable.

* * *

6\. Renaming semantics (subtle but important)
---------------------------------------------

Renaming should:

*   Never change the UUID
*   Be reflected immediately to:
    *   Other clients
    *   Discovery
    *   Machines menu

Renaming is:

*   A **machine property**
*   Not a per-client preference

That matches user expectation:

> “I renamed the server — why doesn’t it say that elsewhere?”

* * *

7\. Where renaming should live in the UI
----------------------------------------

Good places:

*   Machine ▸ Rename…
*   Inline rename in Machines panel (future)
*   In the Machine Settings / Info sheet

Avoid:

*   File menu
*   App Settings

Renaming is about _this machine_, not global config.

* * *

8\. How this helps with your earlier concerns
---------------------------------------------

This directly helps with:

*   Resource management (“Which thing is burning CPU?”)
*   Quit decisions (“Do I really want to stop _Teletext Server_?”)
*   Discovery (“Oh, that’s _the_ fileserver”)
*   Non-technical users (“I don’t care what model it is”)

This is _exactly_ how people think about systems.

* * *

9\. A small but powerful extra (optional, later)
------------------------------------------------

You may eventually want:

*   A **role** or **tag** field:
    *   `fileserver`
    *   `printer`
    *   `teletext`
*   Purely descriptive
*   Not enforced

But don’t over-design it now.  
Names alone already get you 80% of the benefit.