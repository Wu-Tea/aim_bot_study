param([Parameter(Mandatory=$true)][string]$TraceDirectory)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Globalization;
public static class LongTraceValidation {
 public static Dictionary<string,long> Check(string[] paths) {
  var counts=new Dictionary<string,long>{{"V",0},{"C",0},{"D",0},{"errors",0},{"detail_frames",0}};
  string header=null;var cols=new Dictionary<string,int>();long lastV=0,lastC=0,expected=0,seen=0,frame=0;
  foreach(var path in paths) using(var file=new StreamReader(path)) {
   var h=file.ReadLine();if(header==null){header=h;var names=h.Split(',');for(int i=0;i<names.Length;i++)cols[names[i]]=i;}
   else if(h!=header)throw new Exception("Header differs across parts");
   string line;while((line=file.ReadLine())!=null){
    var row=line.Split(',');if(row.Length!=cols.Count){counts["errors"]++;continue;}
    Func<string,long> n=k=>long.Parse(row[cols[k]],CultureInfo.InvariantCulture);
    string lane=row[0];if(!counts.ContainsKey(lane)){counts["errors"]++;continue;}counts[lane]++;
    if(lane=="V"){
     if(lastV!=0 && seen!=expected)counts["errors"]++;
     long seq=n("sequence");if(seq!=lastV+1)counts["errors"]++;lastV=seq;
     expected=n("boxes");seen=0;frame=n("frame_id");
     if(n("detail_enabled")!=0){counts["detail_frames"]++;
      if(!(n("infer_submit_begin_ns")>0 && n("infer_submit_begin_ns")<=n("infer_submit_end_ns") &&
           n("infer_submit_end_ns")<=n("output_submit_begin_ns") && n("output_submit_begin_ns")<=n("gpu_wait_begin_ns") &&
           n("gpu_wait_begin_ns")<=n("gpu_wait_end_ns")))counts["errors"]++;
     }
    }else if(lane=="D"){
     if(n("sequence")!=lastV || n("frame_id")!=frame || n("detection_index")!=seen)counts["errors"]++;seen++;
    }else if(lane=="C"){
     long tick=n("tick_id");if(tick!=lastC+1)counts["errors"]++;lastC=tick;
     if(n("device_begin_ns")!=0 && n("device_end_ns")<n("device_begin_ns"))counts["errors"]++;
    }
   }
  }
  if(lastV!=0 && expected!=seen)counts["errors"]++;
  return counts;
 }
}
'@
$parts=@(Get-ChildItem -LiteralPath $TraceDirectory -Filter 'pipeline*.csv'|Sort-Object @{Expression={if($_.BaseName -eq 'pipeline'){0}else{[int]($_.BaseName -replace 'pipeline-','')}}}|ForEach-Object FullName)
if(!$parts.Count){throw 'No pipeline parts found'}
$counts=[LongTraceValidation]::Check([string[]]$parts)
$done=Get-Content -LiteralPath "$TraceDirectory/completion.json" -Raw|ConvertFrom-Json
$total=$counts['V']+$counts['C']+$counts['D']
$valid=$done.complete -and !$done.writer_failed -and $done.vision_dropped -eq 0 -and $done.controller_dropped -eq 0 -and $total -eq $done.written -and $counts['errors'] -eq 0
$result=@{valid=$valid;parts=$parts.Count;counts=$counts;completion=$done}
$result|ConvertTo-Json -Depth 6|Set-Content -Encoding utf8 "$TraceDirectory/validation-v2.json"
$result|ConvertTo-Json -Depth 6
if(!$valid){throw 'Trace is incomplete or inconsistent; retain evidence and inspect validation-v2.json'}
