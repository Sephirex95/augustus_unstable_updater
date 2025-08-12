# Augustus Unstable release updater
A lightweight C-based updater for Augustus Windows unstable release.
Not an official updater. Works only in Windows, since it uses Windows native libraries. 

## Can be used instead of augustus.exe so you can make sure you're always playing the up-to-date release!

How it works:

1. Check the Product Version metadata on detected augustus.exe, and checking the commit hash after '-', .e.g "4.0.0.816-ea2305365" -> commit hash = "ea2305365".
2. Run a query on GitHub API to check last commit hash on the main repository and compare the hashes.
3. If different hashes -> download the windows unstable build from the server
4. Force-extract it to overwrite the augustus.exe and other files in the current directory
5. Check if overwrite was successful and return.
