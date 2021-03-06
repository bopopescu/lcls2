#------------------------------
"""
Created on 2017-06-14 by Mikhail Dubrovin
Adopted for LCLS2 on 2018-02-15
"""
#------------------------------

#from PyQt5.QtWidgets import QApplication
from psana.graphqt.QWDateTimeSec import QWDateTimeSec, QApplication, sys

#------------------------------

def print_msg() : print('Start convertor Date and time <-> sec')

#------------------------------

def timeconverter() :
    print_msg()
    app = QApplication(sys.argv)
    w = QWDateTimeSec(None, show_frame=True, verb=True)
    w.setWindowTitle('Date and time <-> sec')
    w.show()
    app.exec_()
    del app
    sys.exit('End of app')

#------------------------------

if __name__ == "__main__" :
    timeconverter()
    
#------------------------------


