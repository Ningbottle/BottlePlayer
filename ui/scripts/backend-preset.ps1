# An explicit backend:build preset is authoritative. Callers that really set
# TAURI_ENV_DEBUG can still select by flag; an unset manual build stays Debug.
function Resolve-BackendPreset {
    param(
        [string]$RequestedPreset,
        [string]$DebugFlag
    )
    if ($RequestedPreset) { return $RequestedPreset }
    switch ($DebugFlag) {
        'false' { return 'bottlemusic-release' }
        'true' { return 'bottlemusic-check' }
        '' { return 'bottlemusic-check' }
        default { throw "Unsupported TAURI_ENV_DEBUG value: $DebugFlag" }
    }
}
