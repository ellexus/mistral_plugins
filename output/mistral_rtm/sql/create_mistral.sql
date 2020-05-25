--
-- create_mistral.sql
--
-- This text file should be imported using the command
--      "mysql -u root -p < create_mistral.sql"
--
-- This will set up the MySQL schema needed for the mistral_rtm plug-in. Any
-- user that has the necessary permissions to create databases and users can be
-- used instead of root.
--
-- In summary this will :
--  ~ Create a new database called 'mistral_log'
--  ~ Create two tables to store rule details and related mistral events
--  ~ Create a user called 'mistral' and give it :
--      All permissions on mistral_log.*
--
--

-- Drop any existing database schema
DROP DATABASE IF EXISTS mistral_log;

-- Create Database
CREATE DATABASE mistral_log;

-- Create User mistral and give it permissions
GRANT ALL PRIVILEGES ON mistral_log.* TO 'mistral'@'%' IDENTIFIED BY 'mistral';

-- Create Tables
USE mistral_log;

-- rule_parameters maintains a list of unique contract rules seen by the plug-in
CREATE TABLE mistral_rule_parameters (
    rule_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    label VARCHAR(256) NOT NULL,
    violation_path VARCHAR(256) NOT NULL,
    call_type VARCHAR(45) NOT NULL,
    measurement VARCHAR(13) NOT NULL,
    size_range VARCHAR(64) NOT NULL,
    threshold VARCHAR(64) NOT NULL,
    clusterid INTEGER UNSIGNED DEFAULT "1",
    PRIMARY KEY (rule_id),
    UNIQUE KEY (
        label,
        violation_path,
        call_type,
        measurement,
        size_range,
        threshold,
        clusterid
    )
)
ENGINE=InnoDB;

-- mistral_events contains all alerts generated by mistral, table partitioning
-- will be handled by LSF Spectrum RTM
CREATE TABLE mistral_events (
    scope VARCHAR(6) NOT NULL,
    type VARCHAR(8) NOT NULL,
    time TIMESTAMP NOT NULL,
    host VARCHAR(256) NOT NULL,
    fstype VARCHAR(256) NOT NULL,
    fsname VARCHAR(256) NOT NULL,
    fshost VARCHAR(256) NOT NULL,
    project VARCHAR(256) NOT NULL,
    rule_parameters BIGINT UNSIGNED NOT NULL,
    observed BIGINT UNSIGNED NOT NULL,
    observed_unit VARCHAR(12) NOT NULL,
    observed_time BIGINT UNSIGNED NOT NULL,
    pid INTEGER,
    command VARCHAR(1405),
    file_name VARCHAR(1405),
    groupid VARCHAR(256),
    group_jobid BIGINT UNSIGNED,
    group_indexid INTEGER UNSIGNED,
    id VARCHAR(256),
    clusterid INTEGER UNSIGNED DEFAULT "1",
    jobid BIGINT UNSIGNED,
    indexid INTEGER UNSIGNED,
    submit_time TIMESTAMP NOT NULL,
    log_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    INDEX scope_idx (scope),
    INDEX type_idx (type),
    INDEX time_idx (time),
    INDEX host_idx (host),
    INDEX ids_idx (groupid, id),
    INDEX job_idx (clusterid, jobid, indexid, submit_time),
    FOREIGN KEY (rule_parameters) REFERENCES mistral_rule_parameters(rule_id)
)
ENGINE=InnoDB;
