' =============================================================================
' API Audit: Viper.System.Machine - System Information
' =============================================================================
' Tests: get_Cores, get_Endian, get_Home, get_Host, get_MemFree, get_MemTotal,
'        get_Os, get_OsVer, get_Temp, get_User
' =============================================================================

PRINT "=== API Audit: Viper.System.Machine ==="

' --- get_Os ---
PRINT "--- get_Os ---"
PRINT "OS: "; Viper.System.Machine.get_Os()

' --- get_OsVer ---
PRINT "--- get_OsVer ---"
PRINT "OSVer: "; Viper.System.Machine.get_OsVer()

' --- get_Cores ---
PRINT "--- get_Cores ---"
PRINT "Cores: "; Viper.System.Machine.get_Cores()

' --- get_MemTotal ---
PRINT "--- get_MemTotal ---"
PRINT "MemTotal (bytes): "; Viper.System.Machine.get_MemTotal()

' --- get_MemFree ---
PRINT "--- get_MemFree ---"
PRINT "MemFree (bytes): "; Viper.System.Machine.get_MemFree()

' --- get_Endian ---
PRINT "--- get_Endian ---"
PRINT "Endian: "; Viper.System.Machine.get_Endian()

' --- get_Host ---
PRINT "--- get_Host ---"
PRINT "Host: "; Viper.System.Machine.get_Host()

' --- get_User ---
PRINT "--- get_User ---"
PRINT "User: "; Viper.System.Machine.get_User()

' --- get_Home ---
PRINT "--- get_Home ---"
PRINT "Home: "; Viper.System.Machine.get_Home()

' --- get_Temp ---
PRINT "--- get_Temp ---"
PRINT "Temp: "; Viper.System.Machine.get_Temp()

PRINT "=== Machine Demo Complete ==="
END
