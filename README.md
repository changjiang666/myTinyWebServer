### myTinyWebServer
### need mysql

// 建立yourdb库
##### create database webServerDB;

// 创建user表
##### USE webServerDB;
##### CREATE TABLE user(
#####     username char(50) NULL,
#####     passwd char(50) NULL
##### )ENGINE=InnoDB;

// 添加数据
##### INSERT INTO user(username, passwd) VALUES('name', 'passwd');

### cd build
### cmake ..
### make

##### dockerfile 敬请期待......
