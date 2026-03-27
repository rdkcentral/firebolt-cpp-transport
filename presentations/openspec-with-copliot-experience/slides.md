---

# Spec Driven Development with OpenSpec & Copilot

> A practical guide to using Spec-driven Development (SDD) and AI agents for safe, traceable, and efficient evolution of complex codebases.

_by Sathishkumar Deena Kirupakaran | Mar 2026_

---

# What is Spec-driven Development? (SDD)

```mermaid
flowchart LR
  Spec[Generate Spec] <--> Code[Generate Code]
  Code -->|Improvements| Spec
  Spec -->|Clarifications| Code
  Spec -.->|Review| Code
  Code -.->|Feedback| Spec
```

- Ensures code and requirements stay in sync for fewer bugs.
- Makes changes traceable and collaboration easier.
- Enables AI agents to assist with clear guidance and feedback loops.

---

# Openspec: Spec-based AI Management Tool

> **Note:** OpenSpec Spec-driven development (SDD) empowers AI coding assistants to accelerate every phase—exploring, proposing, applying, and archiving changes—by providing clear, modular specs and traceable workflows.
 
```mermaid
flowchart LR
  A[Explore]
  B[Propose]
  C[Apply]
  D[Archive]
  AI((AI Agents))
  subgraph Spec_and_Planning["(Requirements +  Architecture)"]
    A[Explore]
    B[Propose]
    A --> B
  end
  
  B --> C
  subgraph Implementation["(Code + Tests)"]
    C[Apply]
  end


  subgraph Delivery["(Delivery + DoD)"]
    D[Archive]
  end

  C --> D
  AI -.-> A
  AI -.-> B
  AI -.-> C
  AI -.-> D
```


https://github.com/Fission-AI/OpenSpec

---

# Usecase: Firebolt C++ Transport

https://github.com/rdkcentral/firebolt-cpp-transport

Why this repo?
- Brownfield codebase with unique challenges that require careful change management and traceability.
- More than 90% of our org have Brownfield codebases.
- New functionality handed off from another org lacking requirements and documentation.

What is the objective?
- Explore and create specs for existing code.
- Analyze missing functionality.
- Propose and implement changes.
- Document and share learnings.

> Use VS Code with Copilot and preferrably a simple Agent like GPT-4.1(0x Tokens) from spec generation to code implementation and documentation.

---

# [Explore] Interactive Spec Generation

```sh
$ /ospx-explore "Explore current repository and generate specs for transport layer. Prompt questions 
when you need more info" --output "specs/"
```

```mermaid
flowchart LR
  subgraph Initial Spec Generation
    direction LR
    subgraph CP[Clarification Phase]
        direction TB
        A[Prompt to analyze code] --> B[Agent asks clarifying questions] --> C[Answer questions] --> D[Agent updates the spec]
    end
    subgraph AP[Approval Phase]
        direction TB
        E[Review and approve] --> F[Spec is finalized and used for implementation]
    end
  end
  CP --> AP 

```

---

# [Explore]: AI Generated Specs

```sh
$ ls
specs/
├── cpp_specifics_spec.md
├── header_interfaces_spec.md
├── json_rpc_handling_spec.md
├── transport_layer_spec.md
├── transport_recommendations_spec.md
changes/
├── archive/
```

- Challenge: Specs were monolithic and hard to update
- Approach: Broke down specs into modular components
- Result: Easier to review, update, and track changes

---

# [Explore] AI Generated Recommendations

- Support headers in the transport layer
- Add robust support for JSON-RPC batch requests
- Make retry logic optional and client-driven
- Ensure full JSON-RPC compliance (batch, error reporting)
- Provide async batch handling and granular error reporting
- Document thread safety, callback registration, and error handling

---

# [Propose] New feature: Add Header Support

```sh
$ /ospx-propose "Add header support to transport layer" --spec "header_support_spec.md" --tasks "tasks.md"
...
Refine Proposal
...
$ ls # After proposal finalization
└── add-header-support/
    ├── add_header_support_proposal.md   # Captures the requirement and why this change is needed
    ├── design.md                       # Explains how the change can be implemented
    ├── specs/
    │   └── header_support_spec.md       # Details the expected behavior and API for the change
    └── tasks.md                        # Lists the concrete steps needed to complete the change
```
---

# [Apply] Implementing Tasks with Copilot

```sh
$ /ospx-apply "add-header-support"
```

- [x] Update `Config` struct to include `headers` field
- [x] Update `connect` method to accept and use headers
- [x] Implement header injection in transport layer
- [x] Implement response header retrieval in transport layer
- [x] Expose `getResponseHeader` in `IGateway` interface
- [x] Ensure thread safety and error handling for header operations
- [x] Add unit and integration tests for header injection and retrieval
- [x] Update documentation for new header support
- [ ] Review and archive the change in OpenSpec

---

# [Archive] Documenting and Sharing Learnings

```sh
$ /ospx-archive "add-header-support"
```
Update tasks.md
- [x] Review and archive the change in OpenSpec

- Captures the rationale, design decisions, implementation details, and results of the change.
- Serves as a reference for future changes and promotes knowledge sharing across the team.

---

# Key Takeaways

Openspec Branch
https://github.com/rdkcentral/firebolt-cpp-transport/tree/feature/openspec

- AI accelerates structured change management
- OpenSpec ensures traceability and review
- AI agents improve productivity and documentation
- AI + Openspec also powers Slidev for presentations (Thanks to #comcast-ai-community
)
  - Current slides are Auto Generated from OpenSpec and Copilot chat conversations.
  - Multiple Reusable Skills generated for creating slides based on the effort

---

# Next Steps

- Share learning with team.
- RDK-E Middleware Crews will expand usage of OpenSpec.
- Continue integrating AI into other repositories and workflows


# Q&A
- Questions and discussion

Thank you

---
