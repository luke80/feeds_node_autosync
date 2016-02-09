<?php

/**
 *  Implements hook_feeds_before_import()
 *
 * @param FeedsSource $source
 *   The FeedsSource object of the hooked feed_source...
 */
function feeds_node_autosync_feeds_before_import( FeedsSource $source ) {
	watchdog('feeds_node_autosync',t("Autosync feed triggered for @feed_source_name pageload:@page_load",array('@feed_source_name'=>$source->importer->getConfig()['name'],'@page_load'=>REQUEST_TIME)),NULL,WATCHDOG_DEBUG);
	//	Load the config (perhaps we should standardize the variable name loaded for this?)
	if(is_object($source->importer) && is_array($source->importer->getConfig()) && $source->importer->getConfig()['content_type'])
		$fna = feeds_node_autosync_config($source->importer->getConfig()['content_type'],NULL,TRUE);
	elseif(is_object($source->importer->processor) && $source->importer->processor->getConfig()['bundle']) {
		$fna = feeds_node_autosync_config($source->importer->processor->getConfig()['bundle'],NULL,TRUE);
	} else {
		drupal_set_message("Unable to determine target bundle. Can't determine if Feeds Node Auto-sync needs to be suspended on this import.","warning");
	}
	if(is_object($fna) && $source->importer->getConfig() != $fna->config->importer_config) {
		//	Alert the user that autosync has been temporarily suspended due to "large batch" import.
		drupal_set_message(t("%active_importer importing to %node_type which is configured to auto-feed from %configured_importer. Temporarily suspending auto-sync for this content type until this import completes.",array('%active_importer'=>$source->importer->getConfig()['name'],'%node_type'=>node_type_get_name($source->importer->getConfig()['content_type']),'%configured_importer'=>$fna->config->importer_config->name)));
		if(!is_object($fna->config->active)) {
			drupal_set_message(t("yep, it's not an object."));
			$fna->config->active = clone $fna->default_config->active;
		}
		else
			$fna->config->active_preimport = clone $fna->config->active;	//	Save this to reset the values after the import completes.
		$fna->config->active->value = -1;	//	-1 is a semi-reserved value for 'active' which indicates the temporary suspension
		feeds_node_autosync_config($source->importer->getConfig()['content_type'],$fna);	//	Save the temporary suspension.
	}
	unset($fna);
}

/**
 *  Implements hook_feeds_after_import()
 *
 * @param FeedsSource $source
 *   The FeedsSource object of the hooked feed_source...
 */
function feeds_node_autosync_feeds_after_import( FeedsSource $source ) {
	//	Load the config (perhaps we should standardize the variable name loaded for this?)
	if(is_object($source->importer) && is_array($source->importer->getConfig())) {
		if($source->importer->getConfig()['content_type'])
			$fna = feeds_node_autosync_config($source->importer->getConfig()['content_type'],NULL,TRUE);
		elseif($source->importer->processor->getConfig()['bundle']) {
			$fna = feeds_node_autosync_config($source->importer->processor->getConfig()['bundle'],NULL,TRUE);
		} else {
			//	drupal_set_message("Unable to determine target bundle. Can't determine if Feeds Node Auto-sync needs to be suspended on this import.","warning");
		}
		if($source->importer->getConfig()['name'] != $fna->config->importer->value && $fna->config->active->value == -1) {
			drupal_set_message(t("%active_importer import complete. Resetting 'active' parameter on %configured_importer to %active.",array('%active_importer'=>$source->importer->getConfig()['name'],'%configured_importer'=>$fna->config->importer->value,'%active'=>($fna->config->active_preimport->value==1)?"Active":(($fna->config->active_preimport->value==0)?"Inactive":"Still importing. This is an error. Please deactivate and re-activate this setting in the content type."))));
			$fna->config->active->value = $fna->config->active_preimport->value;
			unset($fna->config->active_preimport);
			feeds_node_autosync_config($source->importer->getConfig()['content_type'],$fna);
		}
		unset($fna);
		watchdog('feeds_node_autosync',t("Autosync feed completed for @feed_source_name pageload:@page_load",array('@feed_source_name'=>$source->importer->getConfig()['name'],'@page_load'=>REQUEST_TIME)),NULL,WATCHDOG_DEBUG);
	} else {
		watchdog('feeds_node_autosync',t("Autosync feed completed with no source importer nor config with which to detect Feeds Node Auto-sync function."),NULL,WATCHDOG_DEBUG);
	}
}

/**
 *  Implements hook_feeds_before_update()
 *
 * @param FeedsSource $source
 *   The FeedsSource object of the hooked feed_source...
 * @param item
 *   The the feed item being updated
 * @param entity_id
 *   The node id string being updated
 */
function feeds_node_autosync_feeds_before_update( FeedsSource $source, $item, $entity_id ) {
	$entity = array_pop(entity_load('node',array($entity_id)));	//	Load the entity. Returns an array, we need the first one (probably only one)
	//	Retrieve the updated date from the database to detect cooldown collision.
	$feeds_items_result = db_select('feeds_item', 'n')
		->fields('n', array('id', 'url', 'entity_id', 'imported'))
		->condition('entity_id', $entity_id)
		->condition('entity_type', 'node')
		->execute()
		->fetchAssoc();
	$fna = feeds_node_autosync_config($source->importer->getConfig()['content_type']);
	if($fna->config->active->value == 1) {
		if( (REQUEST_TIME - $feeds_items_row['imported']) < ($fna->config->cooldown->value) ) {
			//	If the last update happened within the cooldown interval, this update event should be skipped and indicates positively that the node is both updated and appropriately autosyncing.
			drupal_set_message(t('%node_title updated from feed source.',array('%node_title'=>$entity->title)),'status',TRUE);
			$entity->skip = TRUE;
		} else {
			$entity->skip = FALSE;
		}
	}
	unset($feeds_items_result);
	unset($fna);
}

/**
 *  Implements hook_feeds_before_update()
 *
 * @param FeedsSource $source
 *   The FeedsSource object of the hooked feed_source...
 * @param item
 *   The the feed item being updated
 * @param entity_id
 *   The node id string being updated
 */
function feeds_node_autosync_feeds_presave(FeedsSource $source, $entity, $item) {
	//	If the entity is marked for skip - update the feeds_item to do so.
	if($entity->skip) {
		$entity->feeds_item->skip = TRUE;
	}
	//	I wonder if this is where the phantom empty nodes are being created?
}
