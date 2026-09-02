param([string]$PoPath = 'lang/po/es_ES.po')

$ErrorActionPreference = 'Stop'
$path = [IO.Path]::GetFullPath($PoPath)
$lines = [Collections.Generic.List[string]]::new()
$lines.AddRange([IO.File]::ReadAllLines($path, [Text.Encoding]::UTF8))
$families = @{
    '<combat_noise_warning>' = @('¡Eso suena a problemas!','Hay combate cerca. Manténganse atentos.','Ese ruido no promete nada bueno.','Más vale prepararnos.');
    '<heal_self>' = @('Necesito curarme.','Voy a atender esta herida.','Un momento, tengo que vendarme.');
    '<heal_self_warning>' = @('¡Tengo que vendar esto!','Necesito un momento para detener la hemorragia.','Voy a ocuparme de esta herida.','¡Cubridme mientras me curo!');
    '<heal_self_flavor>' = @('Otra herida más, pero sigo en pie.','He pasado por cosas peores.','Esto va a doler, pero sobreviviré.','No pienso caer por una herida así.');
    '<let_me_pass>' = @('Déjame pasar.','Hazme sitio, <name_g>.','Necesito pasar, <name_g>.','Aparta un momento, <name_g>.');
    '<general_danger>' = @('¡Atentos, se acerca peligro!','¡Prepárense, esto se complica!','¡Ya no estamos a salvo!','¡Ojos abiertos!');
    '<its_safe>' = @('Parece que ya pasó el peligro.','Por ahora estamos a salvo.','La situación se calmó.','Bien, ya no veo amenazas.');
}
$nameTranslations = @{
    'adventurer'='aventurero'; 'dawg'='colega'; 'homie'='compañero';
    'loyal friend'='amigo fiel'; 'muchacho'='muchacho'; 'colleague'='colega'; 'daddy'='amigo'
}

function Repair-Utf8Mojibake([string]$text) {
    if ($text.IndexOf([char]0x00C3) -lt 0 -and $text.IndexOf([char]0x00C2) -lt 0) {
        return $text
    }
    $windows1252 = [Text.Encoding]::GetEncoding(1252)
    return [Text.Encoding]::UTF8.GetString($windows1252.GetBytes($text))
}

$category = ''
$msgid = ''
$changed = 0
$npcAiSource = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq '') { $npcAiSource = $false; $category = '' }
    if ($lines[$i] -like '#: src/npc_ai_*' -or $lines[$i] -like '#: src/npctalk.cpp*') { $npcAiSource = $true }
    if ($lines[$i] -match 'category "(<[^>]+>)"') { $category = $Matches[1]; $msgid = ''; continue }
    if ($lines[$i] -match '^msgid "(.*)"$') { $msgid = $Matches[1]; continue }
    if ($lines[$i] -like 'msgstr *' -and ( $families.ContainsKey($category) -or $category -eq '<name_g>' -or $npcAiSource )) {
        $repaired = Repair-Utf8Mojibake $lines[$i]
        if ($repaired -ne $lines[$i]) { $lines[$i] = $repaired; $changed++ }
    }
    if ($lines[$i] -ne 'msgstr ""') { continue }
    $translation = $null
    if ($category -eq '<name_g>' -and $nameTranslations.ContainsKey($msgid)) {
        $translation = $nameTranslations[$msgid]
    } elseif ($families.ContainsKey($category)) {
        $choices = $families[$category]
        # Stable FNV-1a avoids process/runtime-dependent GetHashCode output.
        [uint32]$hash = 2166136261
        foreach ($character in $msgid.ToCharArray()) {
            $hash = [uint32]((([uint64]($hash -bxor [uint32]$character)) *
                               [uint64]16777619) % [uint64]4294967296)
        }
        $translation = $choices[$hash % $choices.Count]
        if ($msgid -notlike '*<name_g>*') { $translation = $translation.Replace(' <name_g>','').Replace(', <name_g>','') }
        if ($msgid -like '<general_noise_warning>*' -and $translation -notlike '<general_noise_warning>*') {
            $translation = '<general_noise_warning>  ' + $translation
        }
    }
    if ($null -ne $translation) {
        $escaped = $translation.Replace('\','\\').Replace('"','\"')
        $lines[$i] = 'msgstr "' + $escaped + '"'
        $changed++
    }
}
$writer = New-Object IO.StreamWriter($path, $false, (New-Object Text.UTF8Encoding($false)))
$writer.NewLine = "`n"
try {
    foreach ($outputLine in $lines) { $writer.WriteLine($outputLine) }
} finally {
    $writer.Dispose()
}
Write-Host "Filled $changed empty NPC Spanish translations without replacing existing msgstr values."
