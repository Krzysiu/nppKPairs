param([Parameter(Mandatory=$true)][string]$HostDirectory)
$ErrorActionPreference='Stop'
$HostDirectory=(Resolve-Path -LiteralPath $HostDirectory).Path
$exe=Join-Path $HostDirectory 'notepad++.exe'
if(-not (Test-Path -LiteralPath (Join-Path $HostDirectory 'doLocalConf.xml'))){throw 'Only a portable test host is allowed.'}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KPairsTestProcess {
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
 public struct STARTUPINFO { public int cb;public string reserved;public string desktop;public string title;public int x,y,xSize,ySize,xChars,yChars,fill,flags;public short show,reserved2;public IntPtr reservedPtr,input,output,error; }
 [StructLayout(LayoutKind.Sequential)] public struct PROCESS_INFORMATION { public IntPtr process,thread;public uint processId,threadId; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr CreateDesktop(string name,IntPtr device,IntPtr devmode,uint flags,uint access,IntPtr security);
 [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr desktop);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool CreateProcess(string app,string cmd,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string cwd,ref STARTUPINFO startup,out PROCESS_INFORMATION process);
 [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
 [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr process,out uint code);
 [DllImport("kernel32.dll")] public static extern bool TerminateProcess(IntPtr process,uint code);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$name='KPairsTests'+[Guid]::NewGuid().ToString('N')
$desktop=[KPairsTestProcess]::CreateDesktop($name,[IntPtr]::Zero,[IntPtr]::Zero,0,0x10000000,[IntPtr]::Zero)
if($desktop -eq [IntPtr]::Zero){throw "Cannot create isolated test desktop: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
$si=[KPairsTestProcess+STARTUPINFO]::new()
$si.cb=[Runtime.InteropServices.Marshal]::SizeOf($si);$si.desktop=$name
$pi=[KPairsTestProcess+PROCESS_INFORMATION]::new()
try {
 if(-not [KPairsTestProcess]::CreateProcess($exe,('"'+$exe+'" -multiInst -nosession'),[IntPtr]::Zero,[IntPtr]::Zero,$false,0,[IntPtr]::Zero,$HostDirectory,[ref]$si,[ref]$pi)){throw "Cannot start test host: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
 $wait=[KPairsTestProcess]::WaitForSingleObject($pi.process,45000)
 if($wait -ne 0){[void][KPairsTestProcess]::TerminateProcess($pi.process,99);throw 'Host test timed out.'}
 [uint32]$code=0;[void][KPairsTestProcess]::GetExitCodeProcess($pi.process,[ref]$code)
 "Host exit code: $code"
 $report=Join-Path $HostDirectory 'test-result.txt'
 if(Test-Path -LiteralPath $report){Get-Content -LiteralPath $report}else{throw 'No host test report was produced.'}
 if($code -ne 0 -or -not (Select-String -LiteralPath $report -Pattern '^ALL PASSED')){throw 'Host tests failed.'}
} finally {
 if($pi.thread -ne [IntPtr]::Zero){[void][KPairsTestProcess]::CloseHandle($pi.thread)}
 if($pi.process -ne [IntPtr]::Zero){[void][KPairsTestProcess]::CloseHandle($pi.process)}
 [void][KPairsTestProcess]::CloseDesktop($desktop)
}
