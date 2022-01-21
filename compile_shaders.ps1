$srcfolder = "src\shaders"
$dstfolder = "shaders_spv"
$shaders = @(Get-ChildItem $srcfolder\* -Exclude *.glsl)
$libs = @(Get-ChildItem $srcfolder\*.glsl)
$libsdate = [datetime](Get-Date -Date "01/01/1970") # Unix epoch as minimum, please don't go too far back in time
# Get date of last modification of a "library file" (.glsl) to force recompilation of all shaders in this case (I don't want to manage a dependency tree :))
Foreach($lib in $libs)
{
	$d = [datetime](Get-ItemProperty -Path $lib -Name LastWriteTime).lastwritetime
	If($d -gt $libsdate) {
		$libsdate = $d
	}
}

function Show-Notification {
    [cmdletbinding()]
    Param (
        [string]
        $ToastTitle,
        [string]
        [parameter(ValueFromPipeline)]
        $ToastText
    )

    [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] > $null
    $Template = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02)

    $RawXml = [xml] $Template.GetXml()
    ($RawXml.toast.visual.binding.text|where {$_.id -eq "1"}).AppendChild($RawXml.CreateTextNode($ToastTitle)) > $null
    ($RawXml.toast.visual.binding.text|where {$_.id -eq "2"}).AppendChild($RawXml.CreateTextNode($ToastText)) > $null

    $SerializedXml = New-Object Windows.Data.Xml.Dom.XmlDocument
    $SerializedXml.LoadXml($RawXml.OuterXml)

    $Toast = [Windows.UI.Notifications.ToastNotification]::new($SerializedXml)
    $Toast.Tag = "PowerShell"
    $Toast.Group = "PowerShell"
    $Toast.ExpirationTime = [DateTimeOffset]::Now.AddMinutes(1)

    $Notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier("PowerShell")
    $Notifier.Show($Toast);
}

Foreach($shader in $shaders)
{
	$name = [System.IO.Path]::GetFileNameWithoutExtension($shader)
	$filename = [System.IO.Path]::GetFileName($shader)
	$exists = Test-Path -Path $dstfolder\$filename.spv -PathType Leaf
	$d = [datetime](Get-ItemProperty -Path $shader -Name LastWriteTime).lastwritetime
	If($exists) {
		$d2 = [datetime](Get-ItemProperty -Path $dstfolder\$filename.spv -Name LastWriteTime).lastwritetime
	}
	$baseColor = $host.UI.RawUI.ForegroundColor
	If(-not $exists -or $d -gt $d2 -or $libsdate -gt $d2)
	{
		$time = Get-Date -Format "HH:mm:ss"
		#Write-Host "[$time] " -ForegroundColor DarkGray -NoNewLine 
		#Write-Output "Compiling $filename to $dstfolder\$filename.spv"
		$host.UI.RawUI.ForegroundColor = 'Red'
		glslc.exe -O -g --target-env=vulkan1.2 -I$srcfolder $shader -o $dstfolder\$filename.spv
		$dnew = [datetime](Get-ItemProperty -Path $dstfolder\$filename.spv -Name LastWriteTime).lastwritetime
		$host.UI.RawUI.ForegroundColor = $baseColor
		If(-not $exists -or $dnew -lt $time) {
			$now = Get-Date -Format "HH:mm:ss"
			Write-Host "[$now] " -ForegroundColor DarkGray -NoNewLine
			Write-Host "Error: " -ForegroundColor Red -NoNewLine
			Write-Host "Compilation of '" -NoNewLine
			Write-Host "$filename'" -ForegroundColor Cyan -NoNewLine
			Write-Host "' failed ('" -NoNewLine
			Write-Host "$filename.spv" -ForegroundColor Cyan -NoNewLine
			Write-Host "' was not created/updated)."
			Show-Notification -ToastTitle "Shader Compilation Error" -ToastText "[$filename] Compilation failed."
		} Else {
			$now = Get-Date -Format "HH:mm:ss"
			Write-Host "[$now] " -ForegroundColor DarkGray -NoNewLine
			Write-Host "Success: '" -ForegroundColor Green  -NoNewLine
			Write-Host "$filename" -ForegroundColor Cyan -NoNewLine
			Write-Host "' compiled to '" -NoNewLine
			Write-Host "$filename.spv" -ForegroundColor Cyan -NoNewLine
			Write-Host "'."
		}
	} Else {
		#Write-Output "Skiping $filename ($d < $d2)"
	}
}
