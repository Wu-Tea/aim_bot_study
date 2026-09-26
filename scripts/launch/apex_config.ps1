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
    return $Text
}
