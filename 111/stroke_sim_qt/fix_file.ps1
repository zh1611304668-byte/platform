$path = "d:\Desktop\project-rawing\platform\111\stroke_sim_qt\mainwindow.cpp"
$lines = Get-Content $path
# Remove lines 1126 to 1150 (inclusive, 1-based)
# Indices 1125 to 1149 (inclusive, 0-based)
# Keep 0..1124
# Keep 1150..End
$newLines = $lines[0..1124] + $lines[1150..($lines.Count-1)]
$newLines | Set-Content $path -Encoding UTF8
Write-Host "Fixed file lines"
