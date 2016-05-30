/* populate_multiple_tables.sql
 *
 * Eric Martin at Ellexus - 14/03/2016
 *
 * This text file should be imported using the command "mysql -u root -p <
 * create_multiple_tables.sql". This will populate the tables needed for the
 * mistral_mysql_log plugin with random data. This is to check the correct truncating
 * procedure in the end_of_day.sql script.
 *
 * In summary this will :
 *  Populate all the 32 log tables with 5 sets of garbage data
 */

 USE multiple_mistral_log;

 Drop procedure if exists populate;


 -- ---------------------------populate()---------------------------------------
 delimiter $$
 create procedure populate()
    begin

    declare log_max int unsigned default 32;
    declare log_max_counter int unsigned default 1;
    declare data_set_num int unsigned default 5;
    declare data_counter int unsigned default 1;

    start transaction;
    WHILE log_max_counter < (log_max +1) DO
        SET data_counter = 1;

        WHILE data_counter < (data_set_num +1) DO
        IF log_max_counter < 10 then
        SET @dynamic_populate = CONCAT('INSERT INTO log_0', log_max_counter,' VALUES(\'Global\', \'Monitor\', \'2016-03-11T18:35:03\',\'TestData\',\'3\',\'A lot\',\'A little\',\'654321\',\'Do Something fun\',\'File0', data_counter,'\',\'1\',\'1\',\'\')');
        ELSE SET @dynamic_populate = CONCAT('INSERT INTO log_', log_max_counter,' VALUES(\'Global\', \'Monitor\', \'2016-03-11T18:35:03\',\'TestData\',\'3\',\'A lot\',\'A little\',\'654321\',\'Do Something fun\',\'File0', data_counter,'\',\'1\',\'1\',\'\')');
        END IF;
        PREPARE dp from @dynamic_populate;
        EXECUTE dp ;
        DEALLOCATE PREPARE dp;
        SET data_counter = data_counter + 1;
        END WHILE;

        SET log_max_counter = log_max_counter + 1;
    END WHILE;
COMMIT;
END $$

DELIMITER ;

call populate;
