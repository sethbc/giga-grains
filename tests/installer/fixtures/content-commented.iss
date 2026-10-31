; A script whose Content [Files] entry is commented out: check-windows-iss.cmake must refuse it
; (installer_windows_iss_commented).
[Components]
Name: "content"; Description: "Factory content"; Types: full custom

[Files]
; Source: "{#Content}\*"; DestDir: "{commonappdata}\sethbc\Giga Grains"; Components: content
