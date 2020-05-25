--
-- create_mistral.sql
--
-- This text file should be imported using the command
--      "mysql -u root -p < create_multiple_tables.sql"
-- This will set up the MySQL databases and tables needed for the mistral_mysql
-- plugin.
--
-- In summary this will :
--  ~ Create a new database called 'mistral_log'
--  ~ Create 32 tables within 'mistral_log' called 'log_01 .. log_32'
--  ~ Create 32 tables within 'mistral_log' called 'env_01 .. env_32'
--  ~ Create a user called 'mistral' and give it :
--      All permissions on mistral_log.*
--     TODO : Change to grant specific permissions [ ALTER, CREATE, DROP,
--            EXECUTE, INSERT, UPDATE ] permissions on these tables
--
--

-- Drop any existing database schema
DROP DATABASE IF EXISTS mistral_log;

-- Create Database
CREATE DATABASE mistral_log;

-- Create User mistral and give it permissions
GRANT ALL PRIVILEGES ON mistral_log.* TO 'mistral'@'%' IDENTIFIED BY 'mistral';

-- Create Tables for date_table_map and rule_details
USE mistral_log;
CREATE TABLE rule_details (
    rule_id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    label           VARCHAR(256)    NOT NULL,
    violation_path  VARCHAR(256)    NOT NULL,
    call_type       VARCHAR(45)     NOT NULL,
    measurement     VARCHAR(13)     NOT NULL,
    size_range      VARCHAR(64)     NOT NULL,
    threshold       VARCHAR(64)     NOT NULL,
    PRIMARY KEY (rule_id),
    UNIQUE KEY (
        label,
        violation_path,
        call_type,
        measurement,
        size_range,
        threshold
    )
)
ENGINE=InnoDB;

CREATE TABLE date_table_map (
    table_date      DATE            NOT NULL,
    table_num       TINYINT         NOT NULL,
    PRIMARY KEY (table_date),
    UNIQUE KEY (table_num)
)
ENGINE=InnoDB;

-- --------------------------create_log_tables()--------------------------------
-- Procedure which creates 32 tables starting on 01-01-2016
DELIMITER $$
CREATE PROCEDURE create_log_tables()
    BEGIN

    DECLARE log_max INT UNSIGNED DEFAULT 32;
    DECLARE log_counter INT UNSIGNED DEFAULT 1;

    START TRANSACTION;
    WHILE log_counter < (log_max + 1) DO
        SET @dynamic_env = CONCAT('CREATE TABLE env_', LPAD(log_counter, 2, '0'), ' (
                                       plugin_run_id VARCHAR(36) NOT NULL,
                                       env_name VARCHAR(256) NOT NULL,
                                       env_value VARCHAR(256),
                                       env_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY
                                   ) ENGINE=InnoDB;');
        PREPARE cl from @dynamic_env;
        EXECUTE cl;
        DEALLOCATE PREPARE cl;

        SET @dynamic_log = CONCAT('CREATE TABLE log_', LPAD(log_counter, 2, '0'), ' (
                                       scope VARCHAR(6) NOT NULL,
                                       type VARCHAR(8) NOT NULL,
                                       time_stamp DATETIME(6) NOT NULL,
                                       rule_id INT NOT NULL,
                                       observed VARCHAR(64) NOT NULL,
                                       host VARCHAR(256),
                                       fstype VARCHAR(256),
                                       fsname VARCHAR(256),
                                       fshost VARCHAR(256),
                                       pid INT,
                                       cpu INT,
                                       command VARCHAR(1405),
                                       file_name VARCHAR(1405),
                                       group_id VARCHAR(256),
                                       id VARCHAR(256),
                                       mpi_rank INT,
                                       plugin_run_id VARCHAR(36) NOT NULL,
                                       log_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY
                                   ) ENGINE=InnoDB;');
        PREPARE cl from @dynamic_log;
        EXECUTE cl;
        DEALLOCATE PREPARE cl;

        SET log_counter = log_counter + 1;
    END WHILE;
    COMMIT;
END $$

DELIMITER ;
-- -----------------------------------------------------------------------------

CALL create_log_tables();

-- --------------------------populate_date_table_map()---------------------------
-- Procedure which populates the date_table_map table
DELIMITER $$
CREATE PROCEDURE populate_date_table_map()
    BEGIN

    DECLARE table_max INT UNSIGNED DEFAULT 32;
    SET @counter = 1;
    SET @enter_date = CURRENT_DATE() - INTERVAL 32 DAY;

    START TRANSACTION;
    WHILE @counter < (table_max + 1) DO
        INSERT INTO date_table_map (table_date, table_num) VALUES
            (STR_TO_DATE(@enter_date, '%Y-%m-%e'), @counter);
        SET @counter = @counter + 1;
        SET @enter_date = @enter_date + INTERVAL 1 DAY;
    END WHILE;
    COMMIT;
END $$
DELIMITER ;

CALL populate_date_table_map();


DROP PROCEDURE create_log_tables;
DROP PROCEDURE populate_date_table_map;

-- --------------------------------check_exist()--------------------------------
DELIMITER $$
CREATE PROCEDURE check_exist()
    BEGIN
    -- If date is not in control table, set it to be updated
    IF (SELECT NOT EXISTS (SELECT 1
                           FROM date_table_map
                           WHERE table_date = @to_check)) THEN
        SET @date_to_update = @to_check;
        -- Get name of oldest table
        SET @oldest_table_num = (SELECT LPAD(table_num, 2, '0')
                                 FROM date_table_map
                                 ORDER BY table_date
                                 LIMIT 0,1
                                 FOR UPDATE);
        SET @to_truncate_log = CONCAT('TRUNCATE log_', @oldest_table_num,';');
        CALL exec_qry(@to_truncate_log);
        SET @to_truncate_env = CONCAT('TRUNCATE env_', @oldest_table_num,';');
        CALL exec_qry(@to_truncate_env);
        UPDATE date_table_map
            SET table_date = @date_to_update
            WHERE table_num = @oldest_table_num;
        CALL update_eod_tables();
    END IF;

    COMMIT;
END $$
DELIMITER ;
-- -------------------------update_eod_tables()---------------------------------
-- This procedure truncates and then drops the indexes
DELIMITER $$
CREATE PROCEDURE update_eod_tables()
    BEGIN
    -- Check that at least one index exists on the log_nn table
    SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num ',
                              'FROM information_schema.statistics ',
                              'WHERE table_name= \'log_', @oldest_table_num,
                              '\' AND table_schema = \'mistral_log\'');
    CALL exec_qry(@index_count);

    IF @index_num > 1 THEN
        SET @drop = CONCAT('DROP INDEX ScopeIndex ON log_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX TypeIndex ON log_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX TimeStampIndex ON log_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX IDsIndex ON log_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX RunIDIndex ON log_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
    END IF;
    -- Check that at least one index exists on the env_nn table
    SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num ',
                              'FROM information_schema.statistics ',
                              'WHERE table_name= \'env_', @oldest_table_num,
                              '\' AND table_schema = \'mistral_log\'');
    CALL exec_qry(@index_count);

    IF @index_num > 1 THEN
        SET @drop = CONCAT('DROP INDEX RunIndex ON env_', @oldest_table_num, ';');
        CALL exec_qry(@drop);
    END IF;
    COMMIT;
END $$
DELIMITER ;


-- -------------------------update_index()--------------------------------------
-- This procedure tries to add indexes to all older tables
DELIMITER $$
CREATE PROCEDURE update_index()
    BEGIN

    DECLARE counter INT UNSIGNED DEFAULT 0;
    -- Finds the number of tables older than today
    SELECT COUNT(*) INTO @old_table_count FROM date_table_map WHERE (table_date < @date_today);

    WHILE counter < @old_table_count DO
        SET @counter = counter;
        -- Retrieves the name of the old table corresponding to the counter
        SET @to_run = CONCAT('SET @older_table_num = (SELECT LPAD(table_num, 2, \'0\') ',
                                                      'FROM date_table_map ',
                                                      'ORDER BY table_date ',
                                                      'LIMIT ', @counter,',1);');
        CALL exec_qry(@to_run);

        -- Check that at least one index exists on the log_nn table
        SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num ',
                                  'FROM information_schema.statistics ',
                                  'WHERE table_name= \'log_', @older_table_num,
                                  '\' AND table_schema = \'mistral_log\'');
        CALL exec_qry(@index_count);

        IF @index_num = 1 THEN
            -- Adds Indexes back into older tables
            SET @index_scope = CONCAT('ALTER TABLE log_', @older_table_num,
                                      ' ADD INDEX ScopeIndex (Scope);');
            CALL exec_qry(@index_scope);
            SET @index_type = CONCAT('ALTER TABLE log_', @older_table_num,
                                     ' ADD INDEX TypeIndex (Type);');
            CALL exec_qry(@index_type);
            SET @index_Time_Stamp = CONCAT('ALTER TABLE log_', @older_table_num,
                                           ' ADD INDEX TimeStampIndex(Time_Stamp);');
            CALL exec_qry(@index_Time_Stamp);
            SET @index_ids = CONCAT('ALTER TABLE log_', @older_table_num,
                                    ' ADD INDEX IDsIndex(Group_ID, ID);');
            CALL exec_qry(@index_ids);
            SET @index_runid = CONCAT('ALTER TABLE log_', @older_table_num,
                                    ' ADD INDEX RunIDIndex(plugin_run_id);');
            CALL exec_qry(@index_runid);
        END IF;
        -- Check that at least one index exists on the env_nn table
        SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num '
                                  'FROM information_schema.statistics '
                                  'WHERE table_name= \'env_', @older_table_num,
                                  '\' AND table_schema = \'mistral_log\'');
        CALL exec_qry(@index_count);

        IF @index_num = 1 THEN
            -- Adds Indexes back into older tables
            SET @index_ids = CONCAT('ALTER TABLE env_', @older_table_num,
                                    ' ADD INDEX RunIndex(plugin_run_id, env_name);');
            CALL exec_qry(@index_ids);
        END IF;
        SET counter = counter + 1;
    END WHILE;
    COMMIT;
END $$
DELIMITER ;
-- -----------------------------------------------------------------------------

-- ---------------------exec_qry( p_sql VARCHAR(100))---------------------------
DELIMITER $$
CREATE PROCEDURE exec_qry( p_sql VARCHAR(255))
    BEGIN
    SET @equery = p_sql;
    PREPARE stmt from @equery;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;
END $$
DELIMITER ;

-- -----------------------------end_of_day()----------------------------------
-- Checks that today's date is not older than any in table and runs

DELIMITER $$
CREATE PROCEDURE end_of_day()
BEGIN

    SET @date_today = CURRENT_DATE();
    SET @date_tomorrow = @date_today + INTERVAL 1 DAY;
    SET @oldest_date = (SELECT MIN(table_date) FROM date_table_map);

    IF @oldest_date < @date_today THEN
        -- Enters loop if today's table does not exist
        SET @to_check = @date_today;
        CALL check_exist();

        -- Enters loop if tomorrow's table does not exist
        SET @to_check = @date_tomorrow;
        CALL check_exist();

        CALL update_index();
    END IF;

END $$
DELIMITER ;

-- -----------------------------------------------------------------------------

-- Runs the end of day procedure to set up today and tomorrow
CALL end_of_day();
