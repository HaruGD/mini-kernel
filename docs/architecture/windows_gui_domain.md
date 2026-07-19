# Windows GUI Domain And Compatibility Runtime

This document defines a feasible long-term architecture for using a
user-supplied Windows VM as both a Windows application runtime and the default
normal-session presentation domain for OS64. It replaces the less precise idea
of "Windows as the GUI server" with an explicit trust, ownership, recovery, and
implementation model.

This is a post-native-desktop research goal. It does not change the current
Phase 4 implementation order, and it does not permit the native display,
input, or recovery paths to be removed.

## 1. Goal

The primary product motivations are:

1. run Windows applications that have no native OS64 build or practical native
   substitute; and
2. play legitimately accessed DRM-protected video through the original
   Windows application or browser, its licensed content-decryption system, and
   the Windows protected media and output stack.

The Windows presentation domain is intended to be the finished normal-user
experience, not a temporary migration screen. It makes those capabilities feel
like part of one desktop without trusting Windows with Host policy or removing
the native desktop and recovery implementation.

### Normal-Mode User Experience

During an ordinary session the user sees one continuously composed desktop:

- a Windows application creates its ordinary Guest HWND and renders through
  the original Windows graphics and media stacks;
- an OS64-native application creates a Host-authoritative window that appears
  as a matched proxy HWND;
- DWM and the Windows display stack place both kinds of window on the same
  desktop and the integrated shell lists both kinds of application;
- launching, focusing, minimizing, or closing a Windows application does not
  open a visible VM container, replace the desktop, or switch display modes;
- protected video remains on the same direct Windows GPU output instead of
  crossing into a Host compositor.

This is why Windows owns normal presentation: it gives the cleanest unified
desktop, preserves Windows application behavior, and gives legitimate DRM
playback the best chance of retaining its expected protected path. Native
presentation still exists for boot, secure Host UI, administration, and
failure recovery; those exceptional transitions are not the ordinary app
workflow.

OS64 remains the system owner:

- boot and shutdown;
- CPU, memory, process, and thread scheduling;
- storage and filesystem ownership;
- physical input ownership and secure-attention handling;
- device and IOMMU policy;
- native application execution;
- VM creation, destruction, and resource limits;
- security decisions, diagnostics, and recovery;
- the authoritative state of OS64-native windows and surfaces.

The Windows VM provides two runtime roles in the integrated session:

```text
Windows Compatibility Runtime
├─ Windows Application Runtime
│  ├─ Win32 and Windows services
│  ├─ DirectX
│  ├─ licensed protected-media playback
│  └─ supported Windows kernel drivers
└─ Windows Presentation Domain
   ├─ proxy HWNDs for OS64-native windows
   ├─ DWM composition and Windows protected scanout
   └─ an exclusively assigned high-performance GPU
```

The concise description is:

> OS64 owns the machine and all security-relevant state. A Windows VM is the
> default normal-session compatibility and presentation domain, but remains
> replaceable, untrusted, and unnecessary for native recovery.

Windows is not a required boot dependency. If it cannot start or must be
terminated, OS64 returns to a complete native recovery and administration
path.

### End-State Topology

The final integrated system separates presentation, data, input, and security
authority instead of treating the visible desktop as the machine owner:

```text
physical keyboard and pointer
    -> OS64 input drivers
    -> secure-attention detector
    -> Host input router
         ├─ native secure/recovery UI
         └─ virtual HID -> Windows

OS64 native application
    -> Window SDK -> windowd -> winpresentd
    -> bounded surface bridge -> Proxy HWND ---------+
                                                       |
Windows application -> original HWND -----------------+-> DWM/display stack
Windows protected media -> protected surface --------+-> passed-through dGPU
                                                       -> direct HDCP output

OS64 user data -> VFS capability -> fileportald
    -> VirtIO-FS queues -> Windows VirtIO-FS/WinFsp -> Z:\

Windows system, applications, ACLs, and AppData
    -> private VM virtual disk -> C:\

window/app/control metadata
    <-> versioned vsock or dedicated virtual control device

OS64 native recovery -> iGPU/GOP -> independent output profile
```

The visible Windows desktop is therefore a presentation client of Host-owned
native state. It never becomes the source of truth for physical input, Host
files, native windows, permissions, VM lifecycle, or recovery.

## 2. Non-Goals

This project does not:

- reimplement the Windows API or Windows kernel ABI;
- distribute Windows images, product keys, Microsoft binaries, proprietary
  GPU drivers, games, or third-party kernel drivers;
- hide virtualization or bypass VM detection, anti-cheat, DRM enforcement,
  output protection, licensing, or online-service policy;
- extract, capture, map into Host memory, or redistribute decrypted protected
  media;
- make Host-side translation or recapture of Windows application windows the
  normal integrated-session presentation path;
- promise that every Windows kernel driver or application supports a VM;
- promise that a content provider permits playback in the supported VM profile
  or that every title, resolution, codec, and DRM security level is available;
- trust Windows-rendered pixels for Host authentication or permission prompts;
- grant the Windows guest unrestricted access to Host storage or memory;
- require Windows for the native desktop, terminal, diagnostics, or recovery.

## 3. Trust And Authority Model

Using DWM as the normal-mode final compositor makes Windows highly important
to availability, but it must not make Windows authoritative for Host security.

### Host-Authoritative State

The following state always belongs to OS64:

- native `window_id` plus generation;
- native surface object and content generation;
- native application identity and permissions;
- accepted native geometry, visibility, and lifecycle state;
- physical input stream and secure-attention sequence;
- file-sharing capabilities;
- VM identity, guest-memory mappings, and device assignments;
- display-mode ownership and recovery generation;
- the decision to start, stop, reset, or quarantine the VM.

### Guest-Authoritative State

Windows owns only its internal domain state:

- Windows processes and services;
- Windows application windows;
- DWM internal surfaces and composition;
- Windows GPU command submission;
- Windows user-session state.

### Replicated State

The following objects are replicas and never become Host authority:

- proxy HWNDs representing native OS64 windows;
- guest D3D textures containing copied Host surface pixels;
- LunaShell lists of OS64-native applications;
- guest copies of title, icon, geometry, visibility, and focus state.

A compromised Guest Agent may lie about guest processes or pixels. Therefore,
the Host may enforce a file grant for the Windows VM as a whole, but cannot
claim a strong per-Windows-process security boundary based only on a Guest
Agent process name.

## 4. Secure UI Boundary

No security-sensitive Host UI may be rendered as a Windows proxy window.
Windows could imitate that window after compromise.

The following operations require native output and Host-owned input:

- permission and capability approval;
- login or administrator authentication;
- disk encryption and recovery-key entry;
- VM reset, termination, and device reassignment confirmation;
- secure file-sharing approval;
- panic, crash, and recovery controls;
- security warnings whose authenticity matters.

Entering secure UI revokes Guest input, changes the display mode to the native
path, draws through OS64's trusted presentation backend, and returns to the
Windows domain only after an explicit Host decision.

## 5. Host Architecture

The existing application ABI remains unchanged:

```text
native application
    -> Window SDK
    -> windowd: authoritative native window state
         ├─ Native Presentation Backend
         │    -> native composition
         │    -> displayd
         │    -> GOP, iGPU, or native GPU driver
         └─ Windows Presentation Backend
              -> winpresentd
              -> versioned virtual device and shared queues
              -> Windows bridge
```

The component names `winpresentd`, `display-supervisor`, `fileportald`, and
`vmd` are architectural placeholders, not current binaries.

`windowd` continues to own native window policy regardless of active
presentation backend. In native mode it composites current surfaces itself. In
Windows mode it can either export the complete native composite or export
individual native windows. Switching presentation backends never changes an
application's Window SDK contract.

The native backend remains capable of displaying all native windows. It is not
reduced to a boot logo, because recovery must be able to show running native
applications and preserved surfaces after the Windows domain fails.

## 6. Output Topology

An exclusively assigned RTX GPU cannot simultaneously be a Host recovery
device. The physical display topology must be selected before GPU passthrough
is treated as complete. During a healthy normal session, the selected Windows
output remains active for the whole session; opening a native or Windows
application never changes monitor inputs. Output switching is restricted to
session bring-up, secure Host UI, administration, and failure recovery.

### Profile A: Dual Monitor Inputs

```text
AMD iGPU  -> monitor input A: native and recovery
RTX guest -> monitor input B: Windows normal mode
```

This is the recommended first hardware profile and the first candidate for DRM
qualification because Windows owns a direct RTX-to-monitor link. It is simple
and preserves an independent recovery path, but changing monitor inputs may
require a physical button, DDC/CI support, or an external switch. Seamless
automatic switching is not guaranteed.

### Profile B: Host-Owned Scanout

```text
Windows renders on RTX
    -> copy or shared presentation path
    -> Host iGPU scanout
    -> monitor
```

This allows seamless native overlays and recovery, but requires an efficient
cross-GPU frame-transfer path and may add latency or bandwidth cost to every
Windows frame. It is not a baseline DRM profile: protected video cannot be
assumed to permit readback or copying through a Host-visible frame buffer.

Laptop hybrid graphics demonstrates that an ordinary render surface can move
from a render-only dGPU to an iGPU-owned display when one OS and cooperating
drivers manage a cross-adapter resource. It does not prove that an exclusively
passed-through Guest GPU or hardware-protected surface can be imported by the
Host. OS64 treats non-protected cross-GPU presentation as a separately measured
optimization and never uses it as DRM evidence.

### Profile C: Hardware Display Mux

A hardware mux can switch the monitor between Host and Guest GPUs under Host
control. This provides direct Guest scanout and deterministic recovery and may
support protected playback when the complete Guest GPU-to-monitor link meets
the DRM policy. It depends on specific hardware and must not be assumed on
ordinary desktop systems.

OS64 must publish which profile a machine uses. A failure to switch output is a
recoverable platform limitation, not a reason to leave input owned by the
guest.

## 7. Display And Recovery State

The future Windows domain extends, rather than replaces, the Phase 4 console
and GUI handoff:

```text
NATIVE_RECOVERY
    -> VM_BOOTING
    -> BRIDGE_NEGOTIATING
    -> WINDOWS_PRESENTING
    -> RECOVERING
    -> NATIVE_RECOVERY or WINDOWS_PRESENTING
```

These are lifecycle and failure states, not per-application presentation
modes. Once `WINDOWS_PRESENTING` begins, native proxy windows and ordinary
Windows windows remain in that state together until a secure or recovery event
requires Host-native output.

Every transition carries a nonzero Host display-session generation and exact
VM and Guest Agent identities. Old proxy, present, input, and release messages
are rejected after any VM or bridge restart.

`display-supervisor` is responsible for policy and observes:

- VM and vCPU liveness;
- Guest Agent heartbeat and session generation;
- proxy process and LunaShell health;
- DWM/frame-production progress;
- accepted buffer and present-fence progress;
- passed-through GPU and output state;
- Host-to-Guest queue progress;
- physical input ownership.

Failure recovery proceeds in this order:

1. revoke Guest physical input;
2. stop accepting presents for the failed session generation;
3. activate the native output profile;
4. reconstruct the native composite from Host-owned windows and surfaces;
5. present native recovery controls;
6. release or reset failed bridge and passthrough resources safely;
7. restart or quarantine Windows according to bounded policy;
8. rebuild proxy objects from current Host state only after a new handshake;
9. return to Windows output after a complete acknowledged frame.

No Guest heartbeat is sufficient by itself. A responsive agent with a hung
DWM or GPU is still a presentation failure.

## 8. Window Ownership And Cross-Domain Policy

Native and Windows windows have different authorities:

- a native window is authoritative in `windowd` and replicated into one proxy
  HWND when per-window integration is enabled;
- a Windows window is authoritative inside the Windows VM;
- DWM owns final visual ordering only while `WINDOWS_PRESENTING` is active;
- Host security does not depend on DWM ordering or appearance;
- LunaShell requests native-window state changes but `windowd` validates and
  commits them;
- a Host rejection causes the proxy to return to the last accepted state.

Native proxy identity contains at least:

```text
host_boot_generation
vm_session_generation
bridge_generation
window_id
window_generation
surface_content_generation
```

This prevents PID, VM, window-slot, and proxy reuse from reviving stale state.

## 9. Presentation Bridge Protocol

The bridge is a bounded versioned protocol, preferably exposed through a
virtual PCI device with shared-memory queues and interrupt or event signaling.
The exact transport may change without changing the logical protocol.

Required control operations include:

```text
HELLO / CAPABILITIES
SESSION_BEGIN / SESSION_END
CREATE_PROXY / DESTROY_PROXY
SET_METADATA
SET_GEOMETRY / SET_VISIBILITY
IMPORT_BUFFER / RELEASE_BUFFER
DAMAGE
PRESENT
PRESENT_ACK / FENCE
FOCUS_REQUEST / FOCUS_RESULT
INPUT_EVENT
HEARTBEAT
RESET
```

Protocol requirements:

- fixed maximum queue depth, windows, buffers, rectangles, and message size;
- ABI size, version, flags, length, arithmetic, and enum validation;
- no Host or Guest raw pointer in a shared message;
- monotonic request, content, frame, and session generations;
- producer/consumer indexes updated with documented memory ordering;
- no overwrite of an unconsumed ring slot;
- explicit buffer ownership and release fence;
- bounded timeout and idempotent reset;
- full-frame recovery after a dropped or stale partial-damage sequence;
- exact format, color-space, stride, DPI, scale, and cursor semantics;
- malformed Guest messages affect only the Guest domain and cannot corrupt Host
  state.

The first protocol implementation should be tested with a Host-side simulator
and a Windows bridge process before it is attached to the real hypervisor.

## 10. Surface Transfer And Performance

Because the Host iGPU/GOP path and Guest RTX use different devices and address
spaces, complete zero-copy is not a baseline requirement.

### Stage 1: Full Native Desktop Surface

```text
windowd native composite
    -> one shared CPU-visible buffer
    -> one full-screen Windows proxy
    -> DWM
```

This validates transport, damage, synchronization, failure, and input without
cross-domain per-window complexity.

### Stage 2: Per-Window Buffers

```text
one Host native window surface
    -> one bridge buffer stream
    -> one Windows proxy HWND
```

Use double or triple buffering, bounded damage regions, and present fences.
The Guest bridge uploads changed pixels into a D3D texture. Native UI is
expected to be light enough for an initial CPU copy; optimization follows
measurement.

Windows games and graphics-heavy Windows applications render directly inside
the Guest on the assigned RTX and do not use the Host-surface copy path.

## 11. Protected Media And DRM Playback

Protected-media playback is a supported compatibility goal, not a DRM bypass.
The viable high-quality path keeps protected content inside the Windows trust
and output boundary:

```text
licensed Windows application or browser
    -> service-approved DRM/CDM and applicable Windows Protected Media Path
    -> signed Windows media and graphics components
    -> Guest-visible hardware trust plus RTX protected decode/render/scanout
    -> HDCP-capable HDMI or DisplayPort link
    -> monitor
```

The Host presentation bridge carries ordinary OS64-native pixels into Windows.
It never carries decrypted protected video pixels back into OS64, exposes them
as a shared surface, implements capture, or depends on Guest readback. Guest
control messages may report playback state and errors but contain no protected
media payload.

Protected playback is qualified per exact runtime profile. A profile records:

- Windows edition and build;
- application or browser and content-decryption component version;
- GPU, firmware, and signed Windows driver version;
- virtual firmware, TPM, Secure Boot, Guest-visible trusted-execution support,
  and assigned-device configuration;
- monitor, cable, connector, resolution, refresh rate, and negotiated HDCP
  capability;
- streaming service, test title, codec, DRM system, achieved resolution, and
  observed failure code;
- whether the provider permits that VM environment under its current policy.

Windows PlayReady hardware DRM protects keys and decrypted samples in hardware,
and some output policies require HDCP. Meeting the hardware path is necessary
for those profiles but does not force a provider to issue a license to a VM.
Software DRM or a lower resolution may work in another profile, but it is not
used as evidence that high-resolution hardware DRM is complete.

Activating native secure UI may switch away from the Guest output and interrupt
or pause protected playback. That is an acceptable security consequence. OS64
does not weaken secure attention or keep an untrusted Guest output visible just
to preserve a video session.

## 12. Input Routing

All physical input first enters OS64:

```text
physical input
    -> Host input driver
    -> secure-attention detector
    -> Host input router
         ├─ native/recovery target
         └─ virtual HID injection into Windows
```

The integrated hardware profile does not pass a physical keyboard, pointer,
USB input controller, or other secure-attention source directly to Windows.
Only normalized virtual HID events cross into the Guest. Device passthrough for
specialized controllers is a separate explicit grant and cannot include the
Host's last secure recovery input.

The secure-attention sequence is a Host policy with a reserved event path, not
a LunaShell shortcut or a fixed application-visible `Escape` combination. It
is recognized before Guest injection and cannot be remapped, suppressed, or
acknowledged by Windows. Activation:

1. increments the input-session generation;
2. stops new virtual HID injection;
3. drops or drains events from the old generation without replay;
4. clears Guest and native proxy focus;
5. activates native secure output; and
6. presents Host-owned controls for resume, quarantine, reset, or shutdown.

In Windows normal mode, DWM performs final hit testing. Input over a Windows
application remains inside the Guest. Input delivered to a native proxy HWND
is returned by the bridge to the exact Host window identity, where `windowd`
validates focus, session, sequence, timestamp, and coordinates before delivery
to the native application.

This first implementation has an additional Guest-to-Host round trip for
native windows. A lower-latency Host-side hit-test mirror may be added later,
but it must not disagree with the visible DWM ordering.

Secure attention is intercepted before virtual HID injection. It revokes Guest
input even if the VM, Guest Agent, DWM, or RTX is unresponsive. The same input
event is never delivered to both native and Guest paths.

## 13. VM And Hypervisor Prerequisites

Windows GUI integration starts only after OS64 has stable native execution,
threading, SMP, storage, networking, IOMMU, and device lifecycle.

The initial hypervisor platform requires:

- architecture-neutral VM, vCPU, guest-memory, interrupt, and device objects;
- Intel VMX and AMD SVM backends with EPT/NPT;
- bounded VM-exit dispatch and vCPU scheduling;
- virtual APIC, timers, ACPI, PCI, storage, networking, keyboard, pointer, and
  initial display devices;
- Guest UEFI boot and a virtual TPM/Secure Boot path for supported Windows 11
  configurations;
- an explicitly qualified Guest-visible hardware trust path when the selected
  DRM profile requires hardware-protected keys and media samples;
- deterministic guest reset and power state;
- isolated guest memory and explicit DMA mappings;
- a supervised `vmd` for lifecycle policy while latency-critical VM-exit,
  interrupt, and IOMMU mechanisms remain in the kernel.

Windows must first boot and remain stable on virtual devices. GPU passthrough
is not part of the first Windows boot milestone.

## 14. GPU Passthrough

Exclusive RTX assignment is a separate hardware milestone. Required work
includes:

- IOMMU DMA and interrupt remapping;
- PCIe isolation/ACS validation where available;
- exclusive Host/Guest ownership transitions;
- BAR and large 64-bit MMIO aperture allocation, including resizable BAR where
  applicable;
- MSI/MSI-X routing;
- device reset and failure containment;
- firmware and power-state handling;
- preservation of the Windows protected decode, protected scanout, and output
  protection path without Host mapping or readback;
- rejection when the GPU shares an unsafe isolation group;
- recovery when the device cannot be reset without a platform reboot.

The baseline never assigns the same GPU to Host and Guest simultaneously.
Consumer hardware support must be proven on an explicit hardware matrix rather
than inferred from model name alone.

Live VM save/restore is not assumed while a physical GPU is attached. Storage
snapshots are taken after the Guest is quiesced or stopped, and device state is
recreated through a new assignment session.

## 15. File, Clipboard, And Data Portals

Windows never receives the Host root filesystem or raw Host block device.

### Namespace And Ownership

The final user-visible storage model deliberately separates Windows
compatibility state from Host-owned user data:

| Guest namespace | Authority | Intended content | Normal LunaShell view |
| --- | --- | --- | --- |
| `C:` | Windows VM virtual disk | Windows, Program Files, ProgramData, AppData, registry-dependent and NTFS-sensitive state | hidden from ordinary home view |
| `Z:` | OS64 VFS through bounded export grants | Desktop, Documents, Downloads, Pictures, and other selected user data | primary home namespace |
| OS64 system roots | OS64 only | kernel, boot, drivers, services, configuration, snapshots | never exported |

Hiding `C:` is a user-experience decision, not a security boundary. Windows
applications and malware still have the Guest permissions granted to them on
`C:`. The native administration UI discloses the VM, Windows build and license,
private disk, shared roots, grant modes, dGPU assignment, and runtime health.

Windows protected-media stores, application installation directories, caches,
memory-mapped databases, and data requiring exact NTFS ACL, alternate-stream,
reparse-point, or locking semantics remain on `C:`. OS64 does not redirect all
of `C:\Users` or `AppData` into `Z:` merely to make the namespace look uniform.

### Delivery Sequence

The first implementation remains a copy portal:

1. the user selects a Host file or directory in native trusted UI;
2. OS64 creates an expiring capability for the Windows VM;
3. a broker copies approved data into a bounded exchange area;
4. the Guest imports or exports through the bridge;
5. OS64 validates destination, size, name, and final commit; and
6. the capability expires or is revoked.

Live `Z:` sharing follows only after the copy portal, VFS object identity,
snapshot, and revocation contracts are stable. The future live path uses a
VirtIO-FS-compatible device because it provides file-level Host/Guest access
without exposing a Host block device or storage network.

### Host File Service Boundary

Complex Guest FUSE requests are never parsed in the OS64 kernel. The split is:

```text
OS64 kernel
    -> bounded VirtIO PCI configuration and virtqueues
    -> Guest-memory range, direction, and lifetime validation
    -> interrupt/event delivery
    -> exact VM and device-session generation
    -> VFS handle and permission enforcement

fileportald, sandboxed user service
    -> VirtIO-FS/FUSE request parsing
    -> capability-rooted object lookup
    -> naming and filesystem-semantics translation
    -> quotas, rate limits, audit, and revocation policy
    -> VFS operations through attenuated handles
```

The name `fileportald` is an architectural placeholder, not a current binary.
It runs with no display, raw input, device assignment, unrestricted filesystem,
VM lifecycle, or arbitrary process-control permission. A crash terminates the
file session, leaves snapshots and Host system state intact, and causes the
Guest mount to fail closed until a new generation is negotiated.

### Windows File Stack

The supported Windows profile contains a signed VirtIO-FS PCI driver, WinFsp,
and a supervised Windows VirtIO-FS service installed from the OS64 Tools ISO.
The service mounts the negotiated export as `Z:` or another recorded drive
letter. The mount is a filesystem proxy, not an assertion that every NTFS
feature is available.

The compatibility profile explicitly tests:

- Windows ACL and OS64 identity mapping;
- case sensitivity and case-only rename;
- Windows reserved names and trailing-dot/space rules;
- UTF encoding and normalization;
- symlinks, junctions, and reparse points;
- alternate data streams and extended attributes;
- sharing modes, byte-range locks, delete-on-close, and atomic rename;
- memory mapping, sparse files, flush, durability, and crash recovery;
- executable, installer, antivirus, and indexing behavior; and
- directory enumeration and change-notification overflow.

Unsupported semantics return a deterministic error. They are not silently
approximated when doing so could corrupt data. Applications that require exact
NTFS behavior use `C:` and may import or export documents through the portal.

### Capability-Rooted Lookup

A Guest never selects an arbitrary Host absolute path. Each export grant
contains at least:

```text
grant_id + grant_generation
Host boot and exact VM session identity
Host directory object identity and object generation
read, create, modify, rename, delete, and metadata rights
maximum bytes, objects, open handles, queue depth, and operation rate
expiry, revocation, and audit identity
snapshot and recovery policy
```

Every lookup begins from the authorized Host directory object. Path
normalization is necessary but insufficient because a Guest may race a symlink
or reparse-point change between validation and open. The VFS operation must
resolve and open relative to the capability root as one protected operation,
apply the configured no-follow/beneath policy, and verify the resulting object
identity before returning a handle.

Open file and directory handles retain the grant and VM-session generation.
Revocation increments the generation, rejects new requests, cancels or drains
old in-flight requests according to the operation contract, and prevents a
stale completion from restoring write authority.

Per-Windows-application restrictions may improve UX but are not a Host security
boundary if the Guest is compromised. Host authorization is issued to the VM
session as a whole unless a stronger independently attested boundary is later
defined.

### Data And Control Planes

VirtIO-FS request queues carry file operations and data. If both sides
negotiate `VIRTIO_FS_F_NOTIFICATION`, the VirtIO-FS notification queue carries
supported FUSE invalidation and lock messages. A file change does not require a
second vsock message merely because it originated in an OS64-native process.

Notification support and queue capacity are never assumed. On unsupported
notifications, overflow, dropped invalidation, or generation mismatch,
`fileportald` increments a directory change generation. LunaShell and the
Windows filesystem service discard affected caches and perform a bounded
rescan. Cache state is an optimization, never authority.

A versioned vsock or dedicated virtual control device carries non-file state:

- session handshake, heartbeat, and capabilities;
- native application and window metadata;
- application launch and open-with requests;
- VM and service health;
- clipboard offer and approval metadata; and
- file-portal UI requests that require native approval.

vsock avoids an IP network dependency but is not authenticated merely by being
local. Every message validates ABI version and size, exact VM and bridge
generation, request sequence, capability, queue bounds, and sender role. File
payloads do not move over this control channel once VirtIO-FS is active.

Clipboard integration follows the same capability rule, is size and format
bounded, and remains disabled in secure native mode.

### Ransomware Containment And Recovery

A read-write `Z:` grant gives a compromised Windows VM the ability to modify or
destroy the granted user data. Virtualization protects unexported Host state;
it does not turn a read-write user share into a ransomware-proof sandbox.

The Host defense is layered:

- immutable snapshots unavailable to the Guest;
- copy-on-write or versioned user-data history with retention and storage
  quotas;
- bounded write, create, delete, rename, handle, and traversal rates;
- auditing by VM session and grant generation;
- signals for unusual rename/delete breadth, extension churn, write volume,
  directory traversal, and content change; and
- an independent network-policy service able to quarantine the VM.

Content entropy is at most one weak signal. Compressed or encrypted legitimate
files can have high entropy, and malware can change data slowly to evade a
threshold. Detection is defense in depth, not proof of compromise.

Containment uses an explicit state machine:

```text
NORMAL_RW
    -> SUSPECTED
    -> WRITE_REVOKED
    -> VM_QUARANTINED
    -> RECOVERY_PENDING
    -> RESTORED or FALSE_POSITIVE
```

Entering `WRITE_REVOKED` increments the grant generation, stops new writes,
resolves in-flight operations by contract, invalidates Guest caches, and
remounts or disconnects the share. Entering `VM_QUARANTINED` revokes Guest
network and optional device grants without destroying evidence.

Automatic policy may revoke writes, preserve evidence, and quarantine the VM.
Restoring or discarding user data requires authenticated native secure UI
unless an administrator has explicitly configured a tested unattended policy.
Rollback restores Host user-data state; it does not restore Windows RAM, DWM,
vCPU, dGPU, or open application state.

Copy-on-write metadata switching may be fast, but the architecture promises no
fixed one-second recovery. Drain, consistency checking, cache invalidation,
mount recreation, application restart, data size, and hardware reset determine
the measured recovery bound for each certified profile.

## 16. Windows Guest Components

The Windows side is split into small replaceable components:

```text
presentation bridge driver
    -> native-surface virtual PCI/shared-memory transport

virtual HID driver
    -> Host-filtered keyboard and pointer events

VirtIO-FS PCI driver + WinFsp + VirtIO-FS service
    -> capability-backed Z: user-data mount

Luna Guest Agent service
    -> versioned control plane, heartbeat, proxy lifecycle, clipboard broker

Proxy Window Host
    -> HWND and D3D texture presentation for native windows

LunaShell
    -> integrated desktop, taskbar, launcher, file UI, and runtime status
```

The first supported configuration keeps Explorer and starts LunaShell as a
normal companion application. Full shell replacement is optional because
official Windows Shell Launcher support depends on Windows edition and has
deployment restrictions. OS64 must publish the exact supported editions and
runtime profile instead of assuming every user ISO can replace Explorer.

The final ordinary LunaShell home view presents `Z:`-backed OS64 user folders
and a unified list of native and Windows applications. It does not present the
VM container or `C:` as the user's primary workspace. Native administration,
however, always exposes the actual runtime topology. LunaShell visual hiding is
never treated as access control, attestation, or evidence that Windows is not
running.

Guest kernel drivers require an appropriate Windows signing and installation
strategy. Development test signing is not a production distribution plan.

## 17. Installation And Distribution

Official OS64 releases contain no Windows image or proprietary third-party
driver:

```text
OS64 release
├─ native OS and recovery GUI
├─ hypervisor and VM manager
├─ open OS64 virtual-device definitions
├─ Windows Guest Agent and LunaShell produced by this project
├─ Windows Runtime Builder
└─ installation and compatibility documentation
```

The user supplies a valid Windows ISO, license, activation credentials, and
vendor GPU driver under their applicable terms.

The first installer uses two virtual optical devices:

```text
CD 1: untouched user-supplied Windows ISO
CD 2: OS64 Tools ISO
      ├─ autounattend.xml
      ├─ signed presentation, virtual HID, and VirtIO-FS drivers
      ├─ compatible WinFsp installer or verified acquisition manifest
      ├─ VirtIO-FS service
      ├─ Guest Agent installer
      ├─ Proxy Window Host
      ├─ optional LunaShell installer
      └─ first-boot setup
```

This is preferred over modifying `boot.wim` and `install.wim` in the first
implementation. Image servicing remains an optional builder feature after the
ordinary Tools ISO path is reliable.

Third-party open components are bundled only when their licenses and release
integrity permit redistribution. Otherwise the Runtime Builder records a
version-pinned, hash-verified acquisition step and does not silently download
or execute an unverified installer.

## 18. Runtime Images, Updates, And Snapshots

The virtual disk interface supports sparse allocation and copy-on-write, but
the on-disk format is selected separately. The architecture does not require
qcow2 unless OS64 deliberately implements and tests it.

Logical layout:

```text
windows-system-base.img       read-only validated base
windows-system-overlay.img    system and update changes
windows-data.img              Guest applications and Windows-private profile state
```

Runtime compatibility metadata records:

```json
{
  "windows_build": "supported build range",
  "bridge_abi": 1,
  "file_bridge_abi": 1,
  "agent_version": "...",
  "shell_version": "...",
  "data_profile": "copy-portal or virtiofs-z",
  "virtiofs_driver": "...",
  "winfsp_version": "...",
  "virtual_hardware_profile": "...",
  "gpu_id": "...",
  "gpu_driver": "..."
}
```

Feature updates are staged against a snapshot and must pass VM boot, bridge,
input, output, recovery, and Windows-application tests before promotion.
Security updates must not be disabled indefinitely merely to avoid bridge
maintenance. An incompatible runtime enters native recovery with a clear
diagnostic rather than booting an unverified presentation domain.

Snapshots are storage and configuration checkpoints unless the active device
profile explicitly supports live device-state capture. With an assigned GPU,
the baseline uses Guest quiesce or shutdown, offline snapshot, restart, and
bridge regeneration.

Windows runtime snapshots and Host user-data snapshots are independent:

```text
Windows runtime checkpoint
    -> private C: virtual disks, VM configuration, bridge compatibility

OS64 user-data history
    -> Host-owned Z: source objects and immutable recovery generations
```

A Windows rollback never silently rolls Host user files backward. A Host
user-data rollback never claims to restore Windows process, registry, memory,
or passed-through device state. Any coordinated recovery records both selected
generations and requires an explicit policy decision.

### Optional Bare-Metal Compatibility Fallback

An independently installed bare-metal Windows boot may remain a last-resort
compatibility profile for a licensed application or content service that
rejects every supported VM profile. It is not part of the integrated Windows
GUI Domain and provides none of its Host authority, native proxy windows,
`fileportald`, secure-attention, or snapshot guarantees while Windows owns the
machine.

The fallback uses a separate boot entry and isolated Windows system storage.
Encryption alone protects OS64 confidentiality, not integrity: bare-metal
Windows with raw access to the same writable disk could still destroy encrypted
partitions or metadata. A certified fallback therefore requires separate
offline/removable storage or hardware/firmware-enforced write isolation for
OS64 system and snapshot media. Data exchange uses an explicit bounded exchange
volume or later OS64 import, not concurrent mounting of active OS64 system
state. Boot selection is transparent to the user, and no project feature
disguises virtualization merely to avoid using the fallback.

## 19. Development Sequence

### Stage 0: Native Prerequisites

- close Phase 4 without console overwrite or `drive`;
- complete the native desktop and recovery path;
- complete Phase 4.5 threading and Phase 4.6 SMP;
- provide stable storage, networking, DMA/IOMMU, and device lifecycle.

Exit gate: OS64 remains administrable and can run native GUI applications
without Windows.

### Stage 1: Bridge Prototype Without A VM

- freeze the bridge ABI and limits;
- implement a Host simulator and Windows Proxy Window Host;
- transport one generated full-screen surface over ordinary development IPC;
- validate damage, fences, resizing, input return, restart, and malformed data.

Exit gate: bridge semantics are correct before hypervisor failures are added.

### Stage 2: Minimal Hypervisor

- bring up VMX/SVM, EPT/NPT, one vCPU, virtual interrupts, and guest reset;
- boot a small test guest;
- add multiple vCPUs only after the single-vCPU lifecycle is deterministic.

Exit gate: repeated start, stop, fault, reset, and memory pressure leave zero
unexplained Host resource drift.

### Stage 3: Windows On Virtual Devices

- provide Guest UEFI, storage, networking, input, and a simple virtual display;
- keep physical input at the Host and inject only through virtual HID;
- install a user-supplied Windows image;
- reach a stable Windows desktop inside one native OS64 window;
- add vTPM/Secure Boot support for the chosen Windows profile.

Exit gate: Windows boots and updates on virtual hardware without passthrough.

### Stage 4: Guest Control Plane

- install the bridge driver and Guest Agent;
- negotiate version/capabilities and heartbeat;
- supervise restart and stale-session cleanup;
- keep Explorer and run LunaShell only as a companion.

Exit gate: Guest Agent or VM restart cannot mutate current Host state with an
old session generation.

### Stage 5: Full-Screen Presentation Bridge

- export the complete native composite to one Windows proxy;
- return proxy input to Host routing;
- test frame pacing, damage, fences, DPI, cursor, and recovery;
- retain the virtual display and native output as fallback.

Exit gate: repeated bridge failure returns to native output without losing
native applications or surfaces.

### Stage 6: IOMMU And RTX Passthrough

- validate the explicit hardware profile;
- assign RTX exclusively to Windows;
- install the user-provided compatible driver;
- prove reset, interrupt, MMIO, DMA isolation, and native-output recovery;
- select and certify one physical output topology;
- validate the Windows protected media path, protected GPU output, HDCP
  negotiation, and representative service playback without Host pixel access.

Exit gate: Guest GPU load cannot DMA outside mapped Guest memory or prevent
Host input recovery and native diagnostics. The certified DRM test profile
either reaches its expected protected resolution or reports a documented
provider, license, driver, hardware, or output-policy incompatibility without
attempting circumvention.

### Stage 7: Per-Window Integration

- export each native window as an independent buffer stream;
- create one generation-tagged proxy HWND per native window;
- synchronize accepted move, resize, visibility, focus, and lifecycle state;
- integrate Windows and native entries in LunaShell UI;
- add clipboard and copy portal after the window path is stable.

Exit gate: proxy and Guest restarts recreate visual replicas from Host state
without killing native applications.

### Stage 8: Shared Data And Containment

- stabilize Host VFS object identity, directory capabilities, and immutable
  user-data snapshots before enabling live sharing;
- implement the sandboxed `fileportald` against a simulated hostile Guest;
- install and supervise the signed Windows VirtIO-FS/WinFsp stack;
- expose only selected user-data roots as `Z:` while retaining Windows system
  and application state on `C:`;
- negotiate VirtIO-FS notifications and prove bounded generation-based rescan
  after unsupported notifications or overflow;
- validate naming, ACL, locking, mapping, rename, delete, flush, cache, crash,
  and unsupported-semantics behavior;
- implement rate limits, immutable history, write revocation, VM quarantine,
  native recovery approval, and false-positive recovery; and
- fault the Guest driver, queues, `fileportald`, VFS, snapshot store, and
  control plane without exposing Host system roots or reviving stale grants.

Exit gate: a malicious or crashed Guest can damage only data covered by an
active write grant; it cannot escape an export root, mutate immutable history,
retain authority after revocation, or make recovery depend on Guest
cooperation. No fixed recovery-time claim is published without measured
evidence for the exact profile.

### Stage 9: Productization

- optional supported-edition shell replacement;
- Windows Runtime Builder and Tools ISO automation;
- compatibility manifests, offline snapshots, update qualification, and
  rollback;
- fault injection, resource accounting, long-duration soak, and hardware
  compatibility matrix;
- publish a separate protected-media matrix by Windows build, player, service,
  DRM level, GPU driver, display link, and achieved resolution;
- publish the Windows/private-disk and Host/shared-data compatibility matrix;
  and
- optionally qualify an isolated bare-metal fallback without treating it as an
  integrated or hidden VM mode.

Exit gate: installation and recovery are reproducible without distributing
Windows or hiding virtualization.

## 20. Current OS64 Mapping

Existing OS64 work already supplies the beginning of this architecture:

| Future responsibility | Current foundation |
| --- | --- |
| native surface ownership | page-backed surface objects and attenuated handles |
| native window authority | `windowd` owner/window/content generations |
| application abstraction | public Window SDK and mapped canvas |
| native presentation | `windowd -> displayd -> GOP` |
| input authority | Host input queue, `inputd`, and `windowd` focus routing |
| display recovery generation | Phase 4H console/GUI handoff contract |
| background execution | Phase 4H drive-free scheduler contract |
| Host file namespace | current VFS; capability-rooted directory export remains future work |
| file-service isolation | user service and permission model; `fileportald` remains future work |
| Guest data transport | IPC/shared-object patterns; VirtIO-FS and vsock remain future work |
| thread and CPU foundation | planned Phase 4.5 and Phase 4.6 |
| Windows VM | post-roadmap hypervisor goal |

The current decision to combine window management and native composition in
`windowd` does not block the plan. The presentation backend can be split after
the native contract is stable and measured.

## 21. Final Invariants

The Windows GUI Domain is acceptable only while all of the following remain
true:

- OS64 boots, diagnoses, updates, and recovers without Windows;
- no Windows-rendered pixel is trusted as secure Host UI;
- physical input and secure attention always terminate at the Host first;
- the integrated profile never passes through the Host's last physical secure
  recovery input;
- native applications use one Window SDK regardless of presentation backend;
- Host native window and surface state survives Guest restart;
- a Guest owns no Host pointer, unrestricted DMA range, Host root filesystem,
  or stale session authority;
- GPU ownership is exclusive and IOMMU constrained;
- bridge queues and resource use are bounded and generation checked;
- unsupported Windows versions, GPUs, or drivers fail into native recovery;
- protected media remains inside the Windows protected path and direct Guest
  output; OS64 neither reads back decrypted frames nor claims universal DRM
  service compatibility;
- Windows compatibility state remains on private `C:` storage, and only
  explicitly granted Host user-data roots appear through `Z:`;
- `C:` hiding is UX only and native administration discloses the real runtime;
- Guest filesystem protocol parsing stays in a sandboxed user service, while
  the kernel exposes only bounded transport, memory, session, handle, and
  permission mechanisms;
- no Guest path string, symlink, reparse point, stale handle, stale completion,
  vsock CID, or Guest process name grants Host authority by itself;
- a read-write export is acknowledged as VM-wide authority over the granted
  data and is protected by immutable Guest-inaccessible history;
- automated ransomware response may revoke, preserve, and quarantine, but data
  rollback follows native policy and has no unmeasured one-second guarantee;
- Windows runtime rollback and Host user-data rollback remain separately
  identified operations;
- Windows and third-party proprietary components are supplied and licensed by
  the user;
- virtualization is disclosed and no compatibility feature attempts detection
  circumvention.

## Official Platform References

- [Microsoft: Desktop Window Manager overview](https://learn.microsoft.com/en-us/windows/win32/dwm/dwm-overview)
- [Microsoft: Shell Launcher overview and edition requirements](https://learn.microsoft.com/en-us/windows/configuration/shell-launcher/)
- [Microsoft: Discrete Device Assignment planning and limitations](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/plan/plan-for-deploying-devices-using-discrete-device-assignment)
- [Microsoft: Windows 11 and VM requirements](https://learn.microsoft.com/en-us/windows/whats-new/windows-11-requirements)
- [Microsoft: adding drivers during Windows Setup](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/add-device-drivers-to-windows-during-windows-setup?view=windows-11)
- [Microsoft: Protected Media Path](https://learn.microsoft.com/en-us/windows/win32/medfound/protected-media-path)
- [Microsoft: PlayReady DRM and output protection](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/playready-client-sdk)
- [Microsoft: cross-adapter resources in hybrid graphics](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-cross-adapter-resources-in-a-hybrid-system)
- [Microsoft: Direct3D protected-resource sessions](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device4-createprotectedresourcesession)
- [OASIS: VirtIO 1.2 file-system and socket devices](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
- [VirtIO-FS: Windows Guest installation](https://virtio-fs.gitlab.io/howto-windows.html)
- [VirtIO-FS: architecture and user-space backend](https://virtio-fs.gitlab.io/design.html)
