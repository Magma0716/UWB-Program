# compute data.pi

## 1 (Create Linux Environment)
open Cygwin.bat

## 2 (Locate File Position)
enter
cd C:\\
cd cygwin64
cd sts-2.1.2

## 3 (compute bitstreams)
BYTES=$(stat -c%s data/zeta.pi)
echo "Small (100k):  <$((BYTES/100000)) blocks"
echo "Medium (512k): <$((BYTES/512000)) blocks"
echo "Large (1M):    <$((BYTES/1000000)) blocks"

## 4 (Decide File Size / bits)
### 100,000 (12.2KB)  small size
### 512,000 (62.5KB)  medium size
### 1,000,000 (122KB) large size
enter 
./assess 100000
./assess 512000
./assess 1000000

## 5
enter 
-> 0 
-> data/data.pi 
-> 1 
-> 0 
-> [bitstreams] 
-> 0

## 6
see ./AlgorithmTesting/finalAnalysisReport.txt

