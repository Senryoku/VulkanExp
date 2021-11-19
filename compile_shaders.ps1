$srcfolder = "src\shaders"
$dstfolder = "shaders_spv"
$shaders = @(Get-ChildItem $srcfolder\*)
Foreach($shader in $shaders)
{
	$name = [System.IO.Path]::GetFileNameWithoutExtension($shader)
	$filename = [System.IO.Path]::GetFileName($shader)
	$exists = Test-Path -Path $dstfolder\$filename.spv -PathType Leaf
	$d = [datetime](Get-ItemProperty -Path $shader -Name LastWriteTime).lastwritetime
	If($exists) {
		$d2 = [datetime](Get-ItemProperty -Path $dstfolder\$filename.spv -Name LastWriteTime).lastwritetime
	}
	If(-not $exists -or $d -gt $d2)
	{
		Write-Output "Compiling $filename to $dstfolder\$filename.spv"
		glslangValidator.exe -V -I$srcfolder $shader -o $dstfolder\$filename.spv
	} Else {
		#Write-Output "Skiping $filename ($d < $d2)"
	}
}
