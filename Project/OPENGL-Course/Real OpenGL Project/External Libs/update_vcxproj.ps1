$xmlPath = "..\Real OpenGL Project.vcxproj"
$xml = [xml](Get-Content $xmlPath)

$ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$ns.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

# 1. Update AdditionalIncludeDirectories
$includeDirs = $xml.SelectNodes("//msb:AdditionalIncludeDirectories", $ns)
foreach ($node in $includeDirs) {
    $current = $node.InnerText
    if ($current -notmatch "External Libs/Lua" -and $current -notmatch "External Libs\\Lua") {
        $node.InnerText = $current.Replace("%(AdditionalIncludeDirectories)", ";`$(ProjectDir)External Libs/Lua;`$(ProjectDir)External Libs/sol2/include;%(AdditionalIncludeDirectories)")
    }
}

# 2. Add ClCompile elements for Lua
# Find an ItemGroup that has ClCompile
$itemGroupClCompile = $xml.SelectSingleNode("//msb:ItemGroup[msb:ClCompile]", $ns)
if ($itemGroupClCompile -ne $null) {
    $luaFiles = Get-ChildItem -Path "Lua" -Filter "*.c" | Where-Object { $_.Name -ne "lua.c" -and $_.Name -ne "luac.c" }
    foreach ($file in $luaFiles) {
        $includePath = "External Libs\Lua\" + $file.Name
        # check if it exists
        $existing = $itemGroupClCompile.SelectSingleNode("msb:ClCompile[@Include='$includePath']", $ns)
        if ($existing -eq $null) {
            $newElement = $xml.CreateElement("ClCompile", "http://schemas.microsoft.com/developer/msbuild/2003")
            $newElement.SetAttribute("Include", $includePath)
            $itemGroupClCompile.AppendChild($newElement) > $null
        }
    }
}

# 3. Add ClInclude elements for Lua headers
$itemGroupClInclude = $xml.SelectSingleNode("//msb:ItemGroup[msb:ClInclude]", $ns)
if ($itemGroupClInclude -ne $null) {
    $luaHeaders = Get-ChildItem -Path "Lua" -Filter "*.h"
    foreach ($file in $luaHeaders) {
        $includePath = "External Libs\Lua\" + $file.Name
        $existing = $itemGroupClInclude.SelectSingleNode("msb:ClInclude[@Include='$includePath']", $ns)
        if ($existing -eq $null) {
            $newElement = $xml.CreateElement("ClInclude", "http://schemas.microsoft.com/developer/msbuild/2003")
            $newElement.SetAttribute("Include", $includePath)
            $itemGroupClInclude.AppendChild($newElement) > $null
        }
    }
}

$xml.Save((Convert-Path $xmlPath))
Write-Host "Updated Real OpenGL Project.vcxproj successfully."
