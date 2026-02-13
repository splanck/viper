' =============================================================================
' API Audit: Viper.Machine - System Information
' =============================================================================
' Tests: get_Cores, get_Endian, get_Home, get_Host, get_MemFree, get_MemTotal,
'        get_OS, get_OSVer, get_Temp, get_User
' =============================================================================

PRINT "=== API Audit: Viper.Machine ==="

' --- get_OS ---
PRINT "--- get_OS ---"
PRINT "OS: "; Viper.Machine.get_OS()

' --- get_OSVer ---
PRINT "--- get_OSVer ---"
PRINT "OSVer: "; Viper.Machine.get_OSVer()

' --- get_Cores ---
PRINT "--- get_Cores ---"
PRINT "Cores: "; Viper.Machine.get_Cores()

' --- get_MemTotal ---
PRINT "--- get_MemTotal ---"
PRINT "MemTotal (bytes): "; Viper.Machine.get_MemTotal()

' --- get_MemFree ---
PRINT "--- get_MemFree ---"
PRINT "MemFree (bytes): "; Viper.Machine.get_MemFree()

' --- get_Endian ---
PRINT "--- get_Endian ---"
PRINT "Endian: "; Viper.Machine.get_Endian()

' --- get_Host ---
PRINT "--- get_Host ---"
PRINT "Host: "; Viper.Machine.get_Host()

' --- get_User ---
PRINT "--- get_User ---"
PRINT "User: "; Viper.Machine.get_User()

' --- get_Home ---
PRINT "--- get_Home ---"
PRINT "Home: "; Viper.Machine.get_Home()

' --- get_Temp ---
PRINT "--- get_Temp ---"
PRINT "Temp: "; Viper.Machine.get_Temp()

PRINT "=== Machine Demo Complete ==="
END
