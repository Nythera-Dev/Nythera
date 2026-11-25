#pragma once

namespace nythera::settings {

  // Coordinates external ConfigService reload notifications for an open Settings window:
  // ignores reloads triggered by Settings' own mutations and defers external reloads while
  // the user is actively editing, applying them as soon as the active edit ends.
  class ExternalReloadGate {
  public:
    class ScopedOwnMutation {
    public:
      explicit ScopedOwnMutation(ExternalReloadGate& gate) noexcept : m_gate(&gate) { ++m_gate->m_ownMutationDepth; }
      ~ScopedOwnMutation() {
        if (m_gate != nullptr && m_gate->m_ownMutationDepth > 0) {
          --m_gate->m_ownMutationDepth;
        }
      }

      ScopedOwnMutation(const ScopedOwnMutation&) = delete;
      ScopedOwnMutation& operator=(const ScopedOwnMutation&) = delete;
      ScopedOwnMutation(ScopedOwnMutation&& other) noexcept : m_gate(other.m_gate) { other.m_gate = nullptr; }
      ScopedOwnMutation& operator=(ScopedOwnMutation&&) = delete;

    private:
      ExternalReloadGate* m_gate = nullptr;
    };

    [[nodiscard]] ScopedOwnMutation scopedOwnMutation() noexcept { return ScopedOwnMutation(*this); }

    [[nodiscard]] bool onExternalReload(bool windowOpen, bool activeUserEdit) noexcept {
      if (m_ownMutationDepth > 0 || !windowOpen) {
        return false;
      }
      if (activeUserEdit) {
        m_pendingReload = true;
        return false;
      }
      m_pendingReload = false;
      return true;
    }

    [[nodiscard]] bool flushIfReady(bool windowOpen, bool activeUserEdit) noexcept {
      if (!m_pendingReload || !windowOpen || activeUserEdit || m_ownMutationDepth > 0) {
        return false;
      }
      m_pendingReload = false;
      return true;
    }

    [[nodiscard]] bool hasPendingReload() const noexcept { return m_pendingReload; }

    void reset() noexcept {
      m_pendingReload = false;
      m_ownMutationDepth = 0;
    }

  private:
    int m_ownMutationDepth = 0;
    bool m_pendingReload = false;
  };

} // namespace nythera::settings
