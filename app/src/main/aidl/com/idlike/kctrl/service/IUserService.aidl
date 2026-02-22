package com.idlike.kctrl.service;

interface IUserService {
    void startBinary(String binaryPath, in String[] args);
    void destroy();
    void exit();
}
