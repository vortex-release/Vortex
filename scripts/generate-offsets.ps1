param([string]$InputDirectory = (Join-Path (Split-Path $PSScriptRoot) 'Updated Offsets'), [switch]$Check)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
Set-StrictMode -Version Latest
$rootPath = Split-Path $PSScriptRoot
$dataPath = Join-Path $rootPath 'Updated Offsets'
$inputPath = (Resolve-Path -LiteralPath $InputDirectory).Path
$requiredFiles = @('offsets.json','client_dll.json','info.json')
$contents = @{}
$hashes = [ordered]@{}
foreach ($name in @('offsets.hpp','client_dll.hpp')) {
    if (Test-Path -LiteralPath (Join-Path $inputPath $name)) { $requiredFiles += $name }
}
foreach ($name in $requiredFiles) {
    $path = Join-Path $inputPath $name
    $contents[$name] = [IO.File]::ReadAllText($path)
    $hashes[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
$globals = $contents['offsets.json'] | ConvertFrom-Json
$schema = ($contents['client_dll.json'] | ConvertFrom-Json).'client.dll'.classes
$info = $contents['info.json'] | ConvertFrom-Json

function Read-Offset($value, [string]$name) {
    if ($null -eq $value) { throw "Missing offset: $name" }
    # Support both cs2-dumper's numeric fields and { offset: number } schema exports.
    $property = $value.PSObject.Properties['offset']
    if ($null -ne $property) { $value = $property.Value }
    [long]$number = 0
    if (-not [long]::TryParse([string]$value, [ref]$number) -or $number -lt 0 -or $number -gt [uint32]::MaxValue) {
        throw "Invalid offset: $name"
    }
    return $number
}
$build = Read-Offset $info.build_number 'build_number'
if ($build -eq 0) { throw 'The offset snapshot must identify a nonzero game build.' }
$timestamp = [DateTimeOffset]::Parse([string]$info.timestamp).ToString('o')
$revision = 'local-build-' + $build + '-' + $hashes['client_dll.json'].Substring(0,12).ToLowerInvariant()
$fields = [ordered]@{
 EffectOrigin = $schema.CEffectData.fields.m_vOrigin
 EffectStart = $schema.CEffectData.fields.m_vStart
 EffectEntity = $schema.CEffectData.fields.m_hEntity
 EntityOwner = $schema.C_BaseEntity.fields.m_hOwnerEntity
 BulletServices = $schema.C_CSPlayerPawn.fields.m_pBulletServices
 AimPunchServices = $schema.C_CSPlayerPawn.fields.m_pAimPunchServices
 ShotsFired = $schema.C_CSPlayerPawn.fields.m_iShotsFired
 Sensitivity = $globals.'client.dll'.dwSensitivity
 SensitivityValue = $globals.'client.dll'.dwSensitivity_sensitivity
 MouseSensitivity = $schema.C_BasePlayerPawn.fields.m_flMouseSensitivity
 FovSensitivity = $schema.C_BasePlayerPawn.fields.m_flFOVSensitivityAdjust
 RecoilIndex = $schema.C_CSWeaponBase.fields.m_flRecoilIndex
 LastShotTime = $schema.C_CSWeaponBase.fields.m_fLastShotTime
 ItemServices = $schema.C_BasePlayerPawn.fields.m_pItemServices
 HasDefuser = $schema.CCSPlayer_ItemServices.fields.m_bHasDefuser
 BombTicking = $schema.C_PlantedC4.fields.m_bBombTicking
 BombSite = $schema.C_PlantedC4.fields.m_nBombSite
 BombBlow = $schema.C_PlantedC4.fields.m_flC4Blow
 BombExploded = $schema.C_PlantedC4.fields.m_bHasExploded
 BombLength = $schema.C_PlantedC4.fields.m_flTimerLength
 BombDefusing = $schema.C_PlantedC4.fields.m_bBeingDefused
 DefuseLength = $schema.C_PlantedC4.fields.m_flDefuseLength
 DefuseCountdown = $schema.C_PlantedC4.fields.m_flDefuseCountDown
 BombDefused = $schema.C_PlantedC4.fields.m_bBombDefused
 BombDefuser = $schema.C_PlantedC4.fields.m_hBombDefuser
 SmokeStartTick = $schema.C_SmokeGrenadeProjectile.fields.m_nSmokeEffectTickBegin
 SmokeCenter = $schema.C_SmokeGrenadeProjectile.fields.m_vSmokeDetonationPos
 FirePositions = $schema.C_Inferno.fields.m_firePositions
 FireBurning = $schema.C_Inferno.fields.m_bFireIsBurning
 FireCount = $schema.C_Inferno.fields.m_fireCount
 FireStartTick = $schema.C_Inferno.fields.m_nFireEffectTickBegin
 GlobalVars = $globals.'client.dll'.dwGlobalVars
 EntityList = $globals.'client.dll'.dwEntityList
 ViewMatrix = $globals.'client.dll'.dwViewMatrix
 ViewAngles = $globals.'client.dll'.dwViewAngles
 LocalController = $globals.'client.dll'.dwLocalPlayerController
 LocalPawn = $globals.'client.dll'.dwLocalPlayerPawn
 BuildNumber = $globals.'engine2.dll'.dwBuildNumber
 Identity = $schema.CEntityInstance.fields.m_pEntity
 CrosshairIndex = $schema.C_CSPlayerPawn.fields.m_iIDEntIndex
 MovementFlags = $schema.C_BaseEntity.fields.m_fFlags
 ActualMoveType = $schema.C_BaseEntity.fields.m_nActualMoveType
 WaterLevel = $schema.C_BaseEntity.fields.m_flWaterLevel
 MovementServices = $schema.C_BasePlayerPawn.fields.m_pMovementServices
 MovementMaxSpeed = $schema.CPlayer_MovementServices.fields.m_flMaxspeed
 MovementFriction = $schema.CPlayer_MovementServices_Humanoid.fields.m_flSurfaceFriction
 IsScoped = $schema.C_CSPlayerPawn.fields.m_bIsScoped
 WaitForNoAttack = $schema.C_CSPlayerPawn.fields.m_bWaitForNoAttack
 SpawnImmunity = $schema.C_CSPlayerPawn.fields.m_bGunGameImmunity
 Clip1 = $schema.C_BasePlayerWeapon.fields.m_iClip1
 WeaponReload = $schema.C_CSWeaponBase.fields.m_bInReload
 ControllerTick = $schema.CBasePlayerController.fields.m_nTickBase
 NextPrimaryTick = $schema.C_BasePlayerWeapon.fields.m_nNextPrimaryAttackTick
 SkyTint = $schema.C_EnvSky.fields.m_vTintColor
 SkyBrightness = $schema.C_EnvSky.fields.m_flBrightnessScale
 ControllerPing = $schema.CCSPlayerController.fields.m_iPing
 ControllerPawn = $schema.CCSPlayerController.fields.m_hPlayerPawn
 ControllerActions = $schema.CCSPlayerController.fields.m_pActionTrackingServices
 RoundKills = $schema.CCSPlayerController_ActionTrackingServices.fields.m_iNumRoundKills
 HighestEntity = $globals.'client.dll'.dwGameEntitySystem_highestEntityIndex
 DesignerName = $schema.CEntityIdentity.fields.m_designerName
 AbsVelocity = $schema.C_BaseEntity.fields.m_vecAbsVelocity
 AbsRotation = $schema.CGameSceneNode.fields.m_angAbsRotation
 AbsScale = $schema.CGameSceneNode.fields.m_flAbsScale
 ViewOffset = $schema.C_BaseModelEntity.fields.m_vecViewOffset
 ThrowStrength = $schema.C_BaseCSGrenade.fields.m_flThrowStrength
 PinPulled = $schema.C_BaseCSGrenade.fields.m_bPinPulled
 ThrowTime = $schema.C_BaseCSGrenade.fields.m_fThrowTime
 ProjectileExploded = $schema.C_BaseCSGrenadeProjectile.fields.m_bExplodeEffectBegan
 SmokeEffect = $schema.C_SmokeGrenadeProjectile.fields.m_bDidSmokeEffect
 ProjectileSpawn = $schema.C_BaseCSGrenadeProjectile.fields.m_flSpawnTime
 Elasticity = $schema.C_BaseEntity.fields.m_flElasticity
 PlayerName = $schema.CBasePlayerController.fields.m_iszPlayerName
 SceneNode = $schema.C_BaseEntity.fields.m_pGameSceneNode
 RenderComponent = $schema.C_BaseEntity.fields.m_pRenderComponent
 Collision = $schema.C_BaseEntity.fields.m_pCollision
 EmbeddedCollision = $schema.C_BaseModelEntity.fields.m_Collision
 Health = $schema.C_BaseEntity.fields.m_iHealth
 MaxHealth = $schema.C_BaseEntity.fields.m_iMaxHealth
 Team = $schema.C_BaseEntity.fields.m_iTeamNum
 LifeState = $schema.C_BaseEntity.fields.m_lifeState
 Origin = $schema.CGameSceneNode.fields.m_vecAbsOrigin
 Dormant = $schema.CGameSceneNode.fields.m_bDormant
 Mins = $schema.CCollisionProperty.fields.m_vecMins
 Maxs = $schema.CCollisionProperty.fields.m_vecMaxs
 WeaponServices = $schema.C_BasePlayerPawn.fields.m_pWeaponServices
 ActiveWeapon = $schema.CPlayer_WeaponServices.fields.m_hActiveWeapon
 AttributeManager = $schema.C_EconEntity.fields.m_AttributeManager
 ItemView = $schema.C_AttributeContainer.fields.m_Item
 ItemDefinition = $schema.C_EconItemView.fields.m_iItemDefinitionIndex
 ModelState = $schema.CSkeletonInstance.fields.m_modelState
 Glow = $schema.C_BaseModelEntity.fields.m_Glow
 GlowColor = $schema.CGlowProperty.fields.m_fGlowColor
 GlowOverride = $schema.CGlowProperty.fields.m_glowColorOverride
 GlowType = $schema.CGlowProperty.fields.m_iGlowType
 GlowTeam = $schema.CGlowProperty.fields.m_iGlowTeam
 GlowRange = $schema.CGlowProperty.fields.m_nGlowRange
 GlowRangeMin = $schema.CGlowProperty.fields.m_nGlowRangeMin
 GlowFlashing = $schema.CGlowProperty.fields.m_bFlashing
 GlowEligible = $schema.CGlowProperty.fields.m_bEligibleForScreenHighlight
 GlowEnabled = $schema.CGlowProperty.fields.m_bGlowing
}
# HPP and JSON exports in the update folder must describe the same snapshot.
function Assert-HppOffset([string]$text,[string]$symbol,$expected,[string]$description) {
    $match = [regex]::Match($text,'\b' + [regex]::Escape($symbol) + '\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*;')
    if (-not $match.Success) { throw "Missing C++ offset: $description" }
    $literal=$match.Groups[1].Value
    $actual=if ($literal.StartsWith('0x')) { [Convert]::ToInt64($literal.Substring(2),16) } else { [long]$literal }
    if ($actual -ne (Read-Offset $expected $description)) { throw "JSON/HPP mismatch for $description. Update both exports from the same dump." }
}
if ($contents.ContainsKey('offsets.hpp')) {
    $globalNames=@{GlobalVars='dwGlobalVars';HighestEntity='dwGameEntitySystem_highestEntityIndex';EntityList='dwEntityList';ViewMatrix='dwViewMatrix';ViewAngles='dwViewAngles';LocalController='dwLocalPlayerController';LocalPawn='dwLocalPlayerPawn';BuildNumber='dwBuildNumber'}
    foreach ($name in $globalNames.Keys) { Assert-HppOffset $contents['offsets.hpp'] $globalNames[$name] $fields[$name] $globalNames[$name] }
}
if ($contents.ContainsKey('client_dll.hpp')) {
    $classes=@{
        CEffectData=@('m_vOrigin','m_vStart','m_hEntity')
        C_Inferno=@('m_firePositions','m_bFireIsBurning','m_fireCount','m_nFireEffectTickBegin')
        C_PlantedC4=@('m_bBombTicking','m_nBombSite','m_flC4Blow','m_bHasExploded','m_flTimerLength','m_bBeingDefused','m_flDefuseLength','m_flDefuseCountDown','m_bBombDefused','m_hBombDefuser')
        CCSPlayer_ItemServices=@('m_bHasDefuser')
        C_CSWeaponBase=@('m_flRecoilIndex','m_fLastShotTime')
        C_CSPlayerPawn=@('m_pAimPunchServices','m_iShotsFired')
        CEntityInstance=@('m_pEntity');CCSPlayerController=@('m_hPlayerPawn','m_pActionTrackingServices');CBasePlayerController=@('m_iszPlayerName')
        CCSPlayerController_ActionTrackingServices=@('m_iNumRoundKills')
        CEntityIdentity=@('m_designerName')
        C_BaseCSGrenade=@('m_flThrowStrength','m_bPinPulled','m_fThrowTime')
        C_BaseCSGrenadeProjectile=@('m_bExplodeEffectBegan','m_flSpawnTime')
        C_SmokeGrenadeProjectile=@('m_bDidSmokeEffect','m_nSmokeEffectTickBegin','m_vSmokeDetonationPos')
        C_BaseEntity=@('m_hOwnerEntity','m_vecAbsVelocity','m_flElasticity','m_pGameSceneNode','m_pRenderComponent','m_pCollision','m_iHealth','m_iMaxHealth','m_iTeamNum','m_lifeState')
        C_BaseModelEntity=@('m_vecViewOffset','m_Collision','m_Glow');CGameSceneNode=@('m_vecAbsOrigin','m_bDormant');CCollisionProperty=@('m_vecMins','m_vecMaxs')
        C_BasePlayerPawn=@('m_pWeaponServices','m_pItemServices');CPlayer_WeaponServices=@('m_hActiveWeapon')
        C_EconEntity=@('m_AttributeManager');C_AttributeContainer=@('m_Item');C_EconItemView=@('m_iItemDefinitionIndex')
        CSkeletonInstance=@('m_modelState')
        CGlowProperty=@('m_fGlowColor','m_glowColorOverride','m_iGlowType','m_iGlowTeam','m_nGlowRange','m_nGlowRangeMin','m_bFlashing','m_bEligibleForScreenHighlight','m_bGlowing')
    }
    foreach ($class in $classes.Keys) {
        $match=[regex]::Match($contents['client_dll.hpp'],'namespace\s+'+[regex]::Escape($class)+'\s*\{([\s\S]*?)\n\s*\}')
        if (-not $match.Success) { throw "Missing C++ schema class: $class" }
        foreach ($field in $classes[$class]) { Assert-HppOffset $match.Groups[1].Value $field $schema.$class.fields.$field "$class.$field" }
    }
}
$lines = @('#pragma once', '#include <cstdint>',
 '// Generated from the user-supplied JSON snapshot recorded in Updated Offsets/source.json.',
 '// Run scripts/generate-offsets.ps1 to regenerate; do not hand-edit.',
 'namespace awareness::cs2::offsets {')
$lines += 'inline constexpr char Revision[] = "' + $revision + '";'
$lines += 'inline constexpr char Timestamp[] = "' + $timestamp + '";'
$lines += 'inline constexpr std::uint32_t ExpectedBuild = ' + $build + ';'
foreach ($name in $fields.Keys) {
    $value = Read-Offset $fields[$name] $name
    $lines += 'inline constexpr std::uintptr_t ' + $name + ' = 0x' + $value.ToString('X') + ';'
}
$lines += '// Entity-list layout assumptions; these are not supplied by schema fields.'
$lines += 'inline constexpr std::uintptr_t EntityTable = 0x10;'
$lines += 'inline constexpr std::uintptr_t EntityStride = 0x70;'
$lines += 'inline constexpr std::uint32_t EntryMask = 0x7FFF;'
$lines += 'inline constexpr std::uint32_t EntriesPerChunk = 512;'
$layoutPath=Join-Path $dataPath 'bone-layout.json'
$layout=Get-Content -LiteralPath $layoutPath -Raw | ConvertFrom-Json
$layoutBuild=Read-Offset $layout.build 'bone layout build'
$lines += '// Non-schema skeletal layout, verified separately for the recorded build.'
$lines += 'inline constexpr std::uint32_t BoneLayoutBuild = ' + $layoutBuild + ';'
foreach ($name in @('Head','Neck','Chest','Pelvis')) {
    $number=Read-Offset $layout.bones.$name ('bone ' + $name)
    if($number -ge 8) { throw 'Target bone is outside the supported frame array.' }
    $lines += 'inline constexpr int Bone' + $name + ' = ' + $number + ';'
}
foreach ($pair in @(@('BoneArray','bone_array'),@('BoneCount','bone_count'),@('BoneStride','bone_stride'))) {
    $number=Read-Offset $layout.($pair[1]) $pair[1]
    if($number -gt 4096 -or $number -eq 0) { throw 'Invalid skeletal layout member.' }
    $lines += 'inline constexpr std::uintptr_t ' + $pair[0] + ' = 0x' + $number.ToString('X') + ';'
}
$renderPath=Join-Path $dataPath 'render-layout.json'
$renderLayout=Get-Content -LiteralPath $renderPath -Raw | ConvertFrom-Json
$lines += '// Build-specific rendering layout; provenance is in Updated Offsets/render-layout.json.'
$lines += 'inline constexpr std::uint32_t RenderLayoutBuild = ' + (Read-Offset $renderLayout.build 'render layout build') + ';'
foreach ($name in @('SceneTimestamp','SceneImageSize','MaterialTimestamp','MaterialImageSize','RenderCallback','RenderVtableSlot','CreateMaterial','PacketStride','PacketSceneObject','PacketMaterial','PacketColor','SceneUpdaterCount','SceneUpdaterArray','UpdaterSceneObject','ViewPass')) {
    $value=Read-Offset $renderLayout.$name ('render ' + $name)
    $lines += 'inline constexpr std::uintptr_t ' + $name + ' = 0x' + $value.ToString('X') + ';'
}
$lines += '}'
$trajectoryPath = Join-Path $dataPath 'trajectory-layout.json'
$trajectory = Get-Content -LiteralPath $trajectoryPath -Raw | ConvertFrom-Json
if ((Read-Offset $trajectory.build 'trajectory build') -ne $build) { throw 'Trajectory layout build mismatch.' }
$lines += 'namespace awareness::cs2::trajectory_offsets {'
foreach ($property in $trajectory.PSObject.Properties) {
    if ($property.Name -cmatch '^[A-Z]') {
        $value=if($property.Name -eq 'GrenadeMask') {
            if([long]$property.Value -ne 8589946881) { throw 'Invalid grenade collision mask.' }
            [long]$property.Value
        } else { Read-Offset $property.Value ('trajectory ' + $property.Name) }
        $lines += 'inline constexpr std::uintptr_t ' + $property.Name + ' = 0x' + $value.ToString('X') + ';'
    }
}
foreach ($property in $trajectory.entry_bytes.PSObject.Properties) {
    if ($property.Value -notmatch '^[0-9a-f]{48}$') { throw 'Invalid trajectory entry bytes.' }
    $bytes=[regex]::Matches($property.Value,'..') | ForEach-Object { '0x'+$_.Value }
    $lines += 'inline constexpr unsigned char ' + $property.Name + 'Bytes[]{' + ($bytes -join ',') + '};'
}
$lines += '}'
$manifest = [ordered]@{
    source = 'Local cs2-dumper snapshot; Updated Offsets is the authoritative input'
    snapshot_id = $revision
    build_number = $build
    timestamp = $timestamp
    sha256 = $hashes
    bone_layout_sha256 = (Get-FileHash -LiteralPath $layoutPath).Hash
    render_layout_sha256 = (Get-FileHash -LiteralPath $renderPath).Hash
    trajectory_layout_sha256 = (Get-FileHash -LiteralPath $trajectoryPath).Hash
}
# Validate the complete input before replacing any project snapshot files.
$header = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
$manifestText = ($manifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine
$headerPath = Join-Path $rootPath 'src\cs2_offsets.hpp'
$manifestPath = Join-Path $dataPath 'source.json'
if ($Check) {
    if (-not (Test-Path -LiteralPath $headerPath) -or [IO.File]::ReadAllText($headerPath) -cne $header) {
        throw 'The generated offset header is stale. Run scripts/generate-offsets.ps1 or build.ps1.'
    }
    $recorded = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if($recorded.bone_layout_sha256 -cne $manifest.bone_layout_sha256) { throw 'Skeletal layout provenance is stale.' }
    if($recorded.render_layout_sha256 -cne $manifest.render_layout_sha256) { throw 'Rendering layout provenance is stale.' }
    if($recorded.trajectory_layout_sha256 -cne $manifest.trajectory_layout_sha256) { throw 'Trajectory layout provenance is stale.' }
    if ($recorded.snapshot_id -cne $revision -or $recorded.build_number -ne $build -or
        ([DateTimeOffset]$recorded.timestamp) -ne ([DateTimeOffset]$timestamp)) {
        throw 'The offset provenance manifest is stale.'
    }
    foreach ($name in $requiredFiles) {
        if ($recorded.sha256.$name -cne $hashes[$name] -or
            (Get-FileHash -LiteralPath (Join-Path $dataPath $name)).Hash -cne $hashes[$name]) {
            throw "Offset snapshot hash mismatch: $name"
        }
    }
    Write-Output "Verified $($fields.Count) offsets and snapshot hashes for build $build"
    return
}
New-Item -ItemType Directory -Path $dataPath -Force | Out-Null
if ($inputPath.TrimEnd('\') -ne $dataPath.TrimEnd('\')) {
    foreach ($name in $requiredFiles) {
        Copy-Item -LiteralPath (Join-Path $inputPath $name) -Destination (Join-Path $dataPath $name) -Force
    }
    # A JSON-only snapshot must not inherit optional C++ exports from an older dump.
    foreach ($name in @('offsets.hpp','client_dll.hpp')) {
        $oldExport = Join-Path $dataPath $name
        if (-not $contents.ContainsKey($name) -and (Test-Path -LiteralPath $oldExport)) {
            Remove-Item -LiteralPath $oldExport -Force
        }
    }
}
foreach ($output in @(@($manifestPath,$manifestText),@($headerPath,$header))) {
    # Preserve timestamps for unchanged content to avoid needless recompilation.
    if (-not (Test-Path -LiteralPath $output[0]) -or [IO.File]::ReadAllText($output[0]) -cne $output[1]) {
        [IO.File]::WriteAllText($output[0],$output[1])
    }
}
Write-Output "Imported $($fields.Count) offsets for build $build ($timestamp)"
