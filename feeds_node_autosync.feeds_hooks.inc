<?php

/**
 *  Implements hook_feeds_before_import()
 *
 * @param FeedsSource $source
 *   The FeedsSource object of the hooked feed_source...
 */
function feeds_node_autosync_feeds_before_import( FeedsSource $source ) {
	$source_bundle = _feeds_node_autosync_retrieve_bundle($source, TRUE);
	$source_importer = _feeds_node_autosync_retrieve_importer($source, TRUE);
	//	Load the config (perhaps we should standardize the variable name loaded for this?)
	if($source_bundle != "Unknown node type")
		$fna = feeds_node_autosync_config($source_bundle,NULL,TRUE);
	else
		$fna = NULL;
	if(is_object($fna) && is_object($fna->config) && is_object($fna->config->importer_config) && $source_importer != $fna->config->importer_config && $fna->config->importer_config->name) {
		watchdog('feeds_node_autosync',t("Auto-sync feed triggered for @feed_source_name pageload:@page_load",array('@feed_source_name'=>$source_importer['name'],'@page_load'=>REQUEST_TIME)),NULL,WATCHDOG_DEBUG);
		//	Alert the user that autosync has been temporarily suspended due to "large batch" import.
		drupal_set_message(t("%active_importer importing to %node_type which is configured to auto-feed from %configured_importer. Temporarily suspending auto-sync for this content type until this import completes.",array('%active_importer'=>$source_importer['name'],'%node_type'=>node_type_get_name($source_bundle),'%configured_importer'=>$fna->config->importer_config->name)));
		if(!is_object($fna->config->active)) {
			watchdog('feeds_node_autosync',t("Auto-sync content_type active setting undefined. Resetting from default. pageload:@page_load",array('@feed_source_name'=>$source_importer['name'],'@page_load'=>REQUEST_TIME)),NULL,WATCHDOG_DEBUG);
			$fna->config->active = clone $fna->default_config->active;
		}
		else
			$fna->config->active_preimport = clone $fna->config->active;	//	Save this to reset the values after the import completes.
		$fna->config->active->value = -1;	//	-1 is a semi-reserved value for 'active' which indicates the temporary suspension
		feeds_node_autosync_config($source_bundle, $fna);	//	Save the temporary suspension.
	} else {
		watchdog('feeds_node_autosync',t("Autosync feed started with without disabling any Auto-sync handlers."),NULL,WATCHDOG_DEBUG);
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
	$node_type = _feeds_node_autosync_retrieve_bundle($source,TRUE);
	$source_importer = _feeds_node_autosync_retrieve_importer($source, TRUE);
	//	Load the config (perhaps we should standardize the variable name loaded for this?)
	if($node_type != "Unknown node type")
		$fna = feeds_node_autosync_config($node_type,NULL,TRUE);
	else
		$fna = NULL;
	if(is_object($fna) && is_array($source_importer)) {
		if($source_importer['name'] != $fna->config->importer->value && $fna->config->active->value == -1) {
			drupal_set_message(t("%active_importer import complete. Resetting 'active' parameter on %configured_importer to %active.",array('%active_importer'=>$source_importer['name'],'%configured_importer'=>$fna->config->importer->value,'%active'=>($fna->config->active_preimport->value==1)?"Active":(($fna->config->active_preimport->value==0)?"Inactive":"Still importing. This is an error. Please deactivate and re-activate this setting in the content type."))));
			$fna->config->active->value = $fna->config->active_preimport->value;
			unset($fna->config->active_preimport);
			feeds_node_autosync_config($node_type,$fna);
		}
		unset($fna);
		watchdog('feeds_node_autosync',t("Autosync feed completed for @feed_source_name pageload:@page_load",array('@feed_source_name'=>$source_importer['name'],'@page_load'=>REQUEST_TIME)),NULL,WATCHDOG_DEBUG);
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
	$node = entity_load('node',array($entity_id));
	$entity = (!is_null($entity_id))?array_pop($node):NULL;	//	Load the entity. Returns an array, we need the first one (probably only one)
	unset($node);
	//	Retrieve the updated date from the database to detect cooldown collision.
	$feeds_items_result = db_select('feeds_item', 'n')
		->fields('n', array('id', 'url', 'entity_id', 'imported'))
		->condition('entity_id', $entity_id)
		->condition('entity_type', 'node')
		->execute()
		->fetchAssoc();
	$node_type = _feeds_node_autosync_retrieve_bundle($source);
	//	Load the config (perhaps we should standardize the variable name loaded for this?)
	if($node_type != "Unknown node type")
		$fna = feeds_node_autosync_config($node_type,NULL,TRUE);
	else
		$fna = NULL;
	if(is_object($fna) && $feeds_items_result && $fna->config->active->value == 1) {
		if( (REQUEST_TIME - $feeds_items_result['imported']) < ($fna->config->cooldown->value) ) {
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
	if(property_exists($entity,"skip") && $entity->skip) {
		$entity->feeds_item->skip = TRUE;
	}
	//	I wonder if this is where the phantom empty nodes are being created?
}

/**
 * 	Implemented by above functions which need to retrieve an attached auto-sync content type from a feed by name
 * 
 * @param FeedsSource $source
 *   The FeedsSource object of the feed_source...
 */
function _feeds_node_autosync_retrieve_bundle(FeedsSource $source,$log_transaction=FALSE) {
	$node_type = "Unknown node type";
	if(is_object($source->importer) && is_array($source->importer->getConfig())) {
		$source_config = $source->importer->getConfig();
	}
	if(is_object($source->importer->processor) && $source->importer->processor->getConfig()) {
		$source_processor_config = $source->importer->processor->getConfig();
	}
	if($source_config['content_type']) {
		$node_type = $source_config['content_type'];
		if($log_transaction)
			watchdog('feeds_node_autosync',t("Auto-sync content type known and retrieved. %node_type",array("%node_type"=>$node_type)),NULL,WATCHDOG_DEBUG);
	} elseif(is_object($source->importer->processor) && $source_processor_config['bundle']) {
		$processor_config = $source_processor_config;
		if(is_array($processor_config)) {
			$node_type = $processor_config['bundle'];
			if($log_transaction)
				watchdog('feeds_node_autosync',t("Auto-sync processor configuration retrieved based on processor for %bundle.",array('%bundle'=>$processor_config['bundle'])),NULL,WATCHDOG_DEBUG);
		} else {
			if($log_transaction)
				watchdog('feeds_node_autosync',t("Auto-sync processor configuration not findable."),NULL,WATCHDOG_DEBUG);
		}
		unset($processor_config);
		if($log_transaction)
			watchdog('feeds_node_autosync',t("Auto-sync processor required for a known content type. %node_type",array("%node_type"=>$node_type)),NULL,WATCHDOG_DEBUG);
	} else {
		if($log_transaction)
			watchdog('feeds_node_autosync',t("Auto-sync feed started with a processor having no known content type."),NULL,WATCHDOG_DEBUG);
		drupal_set_message("Unable to determine target bundle. Can't determine if Feeds Node Auto-sync needs to be suspended on this import.","warning");
	}
	unset($source_config);
	unset($source_processor_config);
	return $node_type;
}

/**
 * 	Implemented by above functions which need to retrieve an attached auto-sync content type from a feed by name
 *
 *	@param FeedsSource $source
 *   The FeedsSource object of the feed_source...
 */
function _feeds_node_autosync_retrieve_importer(FeedsSource $source, $log_transaction=FALSE) {
	$source_importer_config = NULL;
	if(is_object($source->importer) && is_array($source->importer->getConfig())) {
		$source_importer_config = $source->importer->getConfig();
		if($log_transaction)
			watchdog('feeds_node_autosync',t("Auto-sync source importer known and retrieved. %importer_name",array("%importer_name"=>$source_importer_config['name'])),NULL,WATCHDOG_DEBUG);
	}
	return $source_importer_config;
}
