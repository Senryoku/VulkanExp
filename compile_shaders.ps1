$shaders = @(Get-ChildItem shaders\*.frag) + @(Get-ChildItem shaders\*.vert)
Foreach($shader in $shaders)
{
	$name = [System.IO.Path]::GetFileNameWithoutExtension($shader)
	$filename = [System.IO.Path]::GetFileName($shader)
	$exists = Test-Path -Path shaders\$filename.spv -PathType Leaf
	$d = [datetime](Get-ItemProperty -Path $shader -Name LastWriteTime).lastwritetime
	If($exists) {
		$d2 = [datetime](Get-ItemProperty -Path shaders\$filename.spv -Name LastWriteTime).lastwritetime
	}
	If(-not $exists -or $d -gt $d2)
	{
		Write-Output "Compiling $filename"
		glslangValidator.exe -V $shader -o shaders/$filename.spv
	} Else {
		Write-Output "Skiping $filename ($d < $d2)"
	}
}
