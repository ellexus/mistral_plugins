/* create_multiple_tables.sql
 *
 * Eric Martin at Ellexus - 08/03/2016
 *
 * This text file should be imported using the command "mysql -u root -p <
 * create_multiple_tables.sql". This will set up the MySQL databases and tables needed for the
 * mistral_mysql_log plugin.
 *
 * In summary this will :
 *  ~ Create a new database called 'multiple_mistral_log'
 *  ~ Create 30 tables within 'mistral_logs' called 'log_01 .. log_30'
 *  ~ Create a user called 'mistral' and give it :
 *      All permissions on mistral_log.*
 *     TODO : Change to grant specific permissions [ ALTER, CREATE, DROP, EXECUTE, INSERT, UPDATE ] permissions on these tables
 *
 */

# TODO Remove this when releasing
 DROP DATABASE IF EXISTS multiple_mistral_log2;

# Create Database
CREATE DATABASE multiple_mistral_log2;

# Create User Mistral And Give it Permissions
GRANT ALL PRIVILEGES ON multiple_mistral_log2.* TO 'mistral'@'%' IDENTIFIED BY 'mistral';

# Create Tables for control_table and rule_parameters
USE multiple_mistral_log2;
CREATE TABLE rule_parameters (Rule_ID INT NOT NULL AUTO_INCREMENT,
                              Violation_Path VARCHAR(256) NOT NULL,
                              Call_Type VARCHAR(45) NOT NULL,
                              Measurement VARCHAR(13) NOT NULL,
                              PRIMARY KEY (Rule_ID),
                              UNIQUE KEY(Violation_Path,Call_Type,Measurement))
                              ENGINE=InnoDB;

CREATE TABLE control_table (Table_date DATE NOT NULL,
                            Table_name VARCHAR(6) NOT NULL,
                            UNIQUE KEY(Table_name),
                            PRIMARY KEY (Table_date))
                            ENGINE=InnoDB;

-- --------------------------create_log_tables()--------------------------------
-- Procedure which creates 32 tables starting on 01-01-2016
delimiter $$
create procedure create_log_tables()
    begin

    declare log_max int unsigned default 32;
    declare log_counter int unsigned default 1;

    start transaction;
    while log_counter < (log_max + 1) do
    IF log_counter < 10 THEN set @enter_name = concat('log_0',log_counter);
    ELSE set @enter_name = concat('log_',log_counter);
    END IF;

    SET @dynamic_name = CONCAT('CREATE TABLE ', @enter_name, ' (Scope VARCHAR(6) NOT NULL,
                     Type VARCHAR(8) NOT NULL,
                     Time_Stamp VARCHAR(20) NOT NULL,
                     Label VARCHAR(256) NOT NULL,
                     Rule_Parameters INT NOT NULL,
                     Observed VARCHAR(32) NOT NULL,
                     Threshold VARCHAR(32) NOT NULL,
                     PID INT,
                     Command VARCHAR(256),
                     File_name VARCHAR(256),
                     Group_ID VARCHAR(256),
                     ID VARCHAR(256),
                     Log_ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY)
                     ENGINE=InnoDB;');
    PREPARE cl from @dynamic_name;
    EXECUTE cl;
    DEALLOCATE PREPARE cl;

    set log_counter = log_counter + 1;
    END WHILE;
    commit;
end $$

DELIMITER ;
-- -----------------------------------------------------------------------------

call create_log_tables();

-- --------------------------populate_control_table()---------------------------
-- Procedure which populates the control_table
delimiter $$
create procedure populate_control_table()
    begin

    declare table_max int unsigned default 32;
    declare counter int unsigned default 1;
    set @enter_date = '2016-01-01';

    start transaction;
    while counter < (table_max + 1) do
    IF counter < 10 THEN set @enter_table_name = concat('log_0',counter);
    ELSE set @enter_table_name = concat('log_',counter);
    END IF;
        insert into control_table (Table_date, Table_name) VALUES(STR_TO_DATE(@enter_date, '%Y-%m-%e'), @enter_table_name);
        set counter = counter + 1;
        set @enter_date = @enter_date + INTERVAL 1 DAY;
    END WHILE;
    commit;
end $$
DELIMITER ;

call populate_control_table();


drop procedure create_log_tables;
drop procedure populate_control_table;

-- --------------------------------check_exist()--------------------------------
delimiter $$
create procedure check_exist()
    begin
    -- If date is not in control table, set it to be updated
    IF (SELECT NOT EXISTS(SELECT 1 FROM control_table WHERE Table_date = @to_check)) THEN
    SET @date_to_update = @to_check;
    SET @need_truncate = 1;
    -- Get name of oldest table
    SET @oldest_table_name = (SELECT Table_name FROM control_table ORDER BY table_date LIMIT 0,1 FOR UPDATE);
    SET @to_truncate = CONCAT('TRUNCATE ', @oldest_table_name,';');
    CALL exec_qry(@to_truncate);
    UPDATE control_table SET Table_date = @date_to_update WHERE table_name = @oldest_table_name;
    call update_eod_tables();
    END IF;

    COMMIT;
end $$
DELIMITER ;
-- -------------------------update_eod_tables()---------------------------------
-- This procedure truncates and then drops the indexes
delimiter $$
create procedure update_eod_tables()
    begin
    -- Checks that there are 1 indexes
    SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num FROM information_schema.statistics WHERE table_name= \'', @oldest_table_name,'\' AND table_schema = \'multiple_mistral_log2\'');
    CALL exec_qry(@index_count);

    IF @index_num > 1 THEN
    SELECT 'Entered if to drop indexes';
        SET @drop = CONCAT('DROP INDEX ScopeIndex ON ', @oldest_table_name, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX TypeIndex ON ', @oldest_table_name, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX TimeStampIndex ON ', @oldest_table_name, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX LabelIndex ON ', @oldest_table_name, ';');
        CALL exec_qry(@drop);
        SET @drop = CONCAT('DROP INDEX IDsIndex ON ', @oldest_table_name, ';');
        CALL exec_qry(@drop);
    END IF;
    COMMIT;
end $$
DELIMITER ;


-- -------------------------update_index()--------------------------------------
-- This procedure tries to add indexes to all older tables
delimiter $$
create procedure update_index()
    begin

    declare counter int unsigned default 0;
    -- Finds the number of tables older than today
    SELECT COUNT(*) INTO @old_table_num FROM control_table WHERE (table_date < @date_today);

    while counter < @old_table_num do
    SET @counter = counter;
        -- Retrieves the name of the old table corresponding to the counter
        SET @to_run = CONCAT('SET @older_table_name = (SELECT Table_name FROM control_table ORDER BY Table_date LIMIT ', @counter,',1);');
        CALL exec_qry(@to_run);

        -- Checks that there are 1 indexes
        SET @index_count = CONCAT('SELECT COUNT(*) INTO @index_num FROM information_schema.statistics WHERE table_name= \'', @older_table_name,'\' AND table_schema = \'multiple_mistral_log2\'');
        CALL exec_qry(@index_count);

        IF @index_num = 1 THEN
            -- Adds Indexes back into older tables
            SET @index_scope = CONCAT('ALTER TABLE ', @older_table_name,' ADD INDEX ScopeIndex (Scope);');
            CALL exec_qry(@index_scope);
            SET @index_type = CONCAT('ALTER TABLE ', @older_table_name,' ADD INDEX TypeIndex (Type);');
            CALL exec_qry(@index_type);
            SET @index_Time_Stamp = CONCAT('ALTER TABLE ', @older_table_name,' ADD INDEX TimeStampIndex(Time_Stamp);');
            CALL exec_qry(@index_Time_Stamp);
            SET @index_label = CONCAT('ALTER TABLE ', @older_table_name,' ADD INDEX LabelIndex(Label);');
            CALL exec_qry(@index_label);
            SET @index_ids = CONCAT('ALTER TABLE ', @older_table_name,' ADD INDEX IDsIndex(Group_ID, ID);');
            CALL exec_qry(@index_ids);
        END IF;
        SET counter = counter + 1;
    END WHILE;
    COMMIT;
end $$
DELIMITER ;
-- -----------------------------------------------------------------------------

-- ---------------------exec_qry( p_sql VARCHAR(100))---------------------------
delimiter $$
create procedure exec_qry( p_sql VARCHAR(255))
    begin
    set @equery = p_sql;
    PREPARE stmt from @equery;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;
END $$
DELIMITER ;

-- -----------------------------end_of_day()----------------------------------
-- Checks that today's date is not older than any in table and runs

delimiter $$
create procedure end_of_day()
begin

    SET @date_today = CURRENT_DATE();
    SET @date_tomorrow = @date_today + INTERVAL 1 DAY;
    SET @get_oldest_date = CONCAT('SET @oldest_date = (SELECT Table_date FROM control_table ORDER BY Table_date LIMIT 0,1);');
    CALL exec_qry(@get_oldest_date);

    IF @oldest_date < @date_today THEN
        -- Enters loop if today's table does not exist
        SET @to_check = @date_today;
        call check_exist();

        -- Enters loop if tomorrow's table does not exist
        SET @to_check = @date_tomorrow;
        call check_exist();

        call update_index();
    END IF;

END $$
DELIMITER ;

-- -----------------------------------------------------------------------------

# Runs the end of procedure to set up today and tomorrow
SET @date_today = '2016-01-31';
SET @date_tomorrow = @date_today + INTERVAL 1 DAY;
CALL end_of_day();
