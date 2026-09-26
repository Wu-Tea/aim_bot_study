# Derive an Apex config without changing the shared user configuration.
function ConvertTo-ApexConfigText([string]$Text) {
    $overrides = @(
        @('runtime.vision', 'model_path', 'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine'),
        @('gamepad.aim_response_curve', 'algorithm', 'linear')
    )
    foreach ($entry in $overrides) {
        $sectionPattern = '(?ms)^[ \t]*\[' + [regex]::Escape($entry[0]) + '\][ \t]*(?:#[^\r\n]*)?\r?\n(?<body>.*?)(?=^[ \t]*\[|\z)'
        $sections = [regex]::Matches($Text, $sectionPattern)
        if ($sections.Count -ne 1) { throw "Expected one [$($entry[0])] section" }
        $body = $sections[0].Groups['body']
        $keyPattern = '(?m)^[ \t]*' + [regex]::Escape($entry[1]) + '[ \t]*=[ \t]*(?:"[^"\r\n]*"|''[^''\r\n]*'')[ \t]*(?:#[^\r\n]*)?\r?$'
        $settings = [regex]::Matches($body.Value, $keyPattern)
        if ($settings.Count -ne 1) { throw "Expected one string setting $($entry[0]).$($entry[1])" }
        $setting = $settings[0]
        $offset = $body.Index + $setting.Index
        $replacement = $entry[1] + ' = "' + $entry[2] + '"'
        if ($setting.Value.EndsWith("`r")) { $replacement += "`r" }
        $Text = $Text.Remove($offset, $setting.Length).Insert($offset, $replacement)
    }
    # Older user configs may have no recoil section/key. Insert only this
    # override while preserving every other current recoil setting.
    $recoilSections = [regex]::Matches($Text, '(?ms)^[ \t]*\[gamepad\.recoil\][ \t]*(?:#[^\r\n]*)?\r?\n(?<body>.*?)(?=^[ \t]*\[|\z)')
    if ($recoilSections.Count -gt 1) { throw 'Duplicate [gamepad.recoil] section' }
    if ($recoilSections.Count -eq 0) {
        return $Text + "`n[gamepad.recoil]`nhipfire_multiplier = 0.5`n"
    }
    $body = $recoilSections[0].Groups['body']
    $settings = [regex]::Matches($body.Value, '(?m)^[ \t]*hipfire_multiplier[ \t]*=[^\r\n]*\r?$')
    if ($settings.Count -gt 1) { throw 'Duplicate recoil hipfire_multiplier' }
    if ($settings.Count -eq 0) {
        return $Text.Insert($body.Index, "hipfire_multiplier = 0.5`n")
    }
    $setting = $settings[0]
    $replacement = 'hipfire_multiplier = 0.5'
    if ($setting.Value.EndsWith("`r")) { $replacement += "`r" }
    return $Text.Remove($body.Index + $setting.Index, $setting.Length).Insert($body.Index + $setting.Index, $replacement)
}
