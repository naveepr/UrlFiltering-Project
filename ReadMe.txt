URL TEST v3[1][1]
====================
The submission contains the following:
1) url-engine.tar.gz - includes code, Makefile, configuration XMLs' and URL
file used for testing
2) ReadMe.txt

Steps to test
=============
1) Copy over the naveen-url-engine.tar.gz to a folder in linux machine used for testing
2) Untar the .gz file tar -zxvf url-engine-naveen.tar.gz
3) make all -- this will generate the url-engine executable
4) The folder contains the sample config.xml as well as sample urlFile.txt used for
testing.
5) POSIX algorithm testing - ./url-engine posix config-large.xml urlFile-large.txt
   SELF algortithm testing - ./url-engine self config-large.xml urlFile-large.txt

6) You can redirect the output to a file to check the diff
    ./url-engine posix config-large.xml urlFile-large.txt > out1.txt
    ./url-engine self config-large.xml urlFile-large.txt > out2.txt
    diff out1.txt out2.txt

7) Performance Testing
    I have also used an API to calculate the time taken to do the URL matching
    based on each algorithm.
    
    During my testing with urlFile-large.txt whose size is 150KB, it is seen
    that the self algorithm takes less time than POSIX algorithm.

    ./url-engine posix config-large.xml urlFile-large.txt calc_time > out1.txt
    ./url-engine self config-large.xml urlFile-large.txt calc_time > out2.txt

    POSIX time taken is 4.706730 sec
    SELF time taken is 0.721962 sec

    The above time is obtained while testing in a Debian based x86_64 machine.

Algorithm
=========
1) libxml2 API is used to construct the config pattern structure.
2) Based on the algorithm chosen POSIX or SELF the core logic is different.
3) Both will read from the urlFile.txt and try to find a matching pattern from
the config pattern saved in memory.
4) POSIX - The regex based pattern is created and send to the POSIX pattern
matching API.
5) SELF - The solution is a dynamic programming approach which does the
comparison of the URL and the config pattern. To identify the wildcard *
before first / which is different from the normal wildcard *, I am replacing with
a delimiter '|' so that the SELF logic can do the matching accordingly.
6) The time taken is also measured using the clock() method in time.h
