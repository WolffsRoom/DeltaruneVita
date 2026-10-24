$ErrorActionPreference = 'Stop'

Set-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server' -Name fDenyTSConnections -Value 0
Set-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp' -Name UserAuthentication -Value 1

$rdpRules = Get-NetFirewallRule | Where-Object {
    $_.Name -like 'RemoteDesktop*' -or
    $_.DisplayGroup -in @('Remote Desktop', 'Área de Trabalho Remota')
}

if ($rdpRules) {
    $rdpRules | Enable-NetFirewallRule
}

Set-Service -Name TermService -StartupType Automatic
if ((Get-Service -Name TermService).Status -eq 'Running') {
    Restart-Service -Name TermService -Force
} else {
    Start-Service -Name TermService
}

$deadline = (Get-Date).AddSeconds(15)
do {
    Start-Sleep -Milliseconds 500
    $listener = Get-NetTCPConnection -LocalPort 3389 -State Listen -ErrorAction SilentlyContinue
} until ($listener -or (Get-Date) -ge $deadline)

$enabled = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server' -Name fDenyTSConnections).fDenyTSConnections -eq 0
$service = (Get-Service -Name TermService).Status

Add-Type -AssemblyName PresentationFramework
[System.Windows.MessageBox]::Show(
    "RDP habilitado: $enabled`nServiço: $service`nPorta 3389: $([bool]$listener)`nTailscale: 100.82.22.111",
    'DeltaruneVita - RDP',
    'OK',
    'Information'
) | Out-Null
